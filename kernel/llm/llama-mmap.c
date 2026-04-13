/*
 * llama-mmap.c — SASY-backed GGUF model loader
 *
 * Loads the entire model file into a SEG_CODE SASY segment (shared,
 * hash-deduped) so multiple contexts sharing the same model only keep
 * one physical copy.  The GGUF header (metadata + tensor descriptors)
 * is parsed in-place from the segment buffer.
 */

#include "llama-mmap.h"
#include "llama-io.h"
#include "llama-impl.h"
#include "../mm/kheap.h"
#include "../mm/sasy/sasy.h"
#include "../log/klog.h"
#include "../../drivers/storage/bfs.h"
#include "../../lib/libc/string.h"

/* -------------------------------------------------------------------------
 * Internal: string helpers
 * ------------------------------------------------------------------------- */

static int str_eq(const char* a, const char* b)
{
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static void str_copy(char* dst, const char* src, size_t max)
{
    size_t i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

/* -------------------------------------------------------------------------
 * GGUF header parser
 *
 * Parses a GGUF v2/v3 header from an in-memory byte buffer.
 * All KV string values are stored directly into header->kv_store[].val.str.data
 * pointing into the same buffer (no extra allocation needed).
 * ------------------------------------------------------------------------- */

static int parse_gguf_string(const uint8_t* buf, uint64_t buf_sz,
                              uint64_t* off, char* dst, size_t dst_max)
{
    if (*off + 8 > buf_sz) return -1;
    uint64_t slen;
    llm_copy(&slen, buf + *off, 8);
    *off += 8;
    if (*off + slen > buf_sz) return -1;
    uint64_t copy = slen < dst_max - 1 ? slen : dst_max - 1;
    llm_copy(dst, buf + *off, (size_t)copy);
    dst[copy] = '\0';
    *off += slen;
    return 0;
}

static int parse_gguf_val(const uint8_t* buf, uint64_t buf_sz,
                           uint64_t* off, gguf_val_type_t type,
                           gguf_kv_t* kv)
{
#define READ(dst, sz) \
    do { if (*off + (sz) > buf_sz) return -1; \
         llm_copy(&(dst), buf + *off, (sz)); *off += (sz); } while(0)

    switch (type) {
    case GGUF_VAL_UINT8:   READ(kv->val.u8,  1); break;
    case GGUF_VAL_INT8:    READ(kv->val.i8,  1); break;
    case GGUF_VAL_UINT16:  READ(kv->val.u16, 2); break;
    case GGUF_VAL_INT16:   READ(kv->val.i16, 2); break;
    case GGUF_VAL_UINT32:  READ(kv->val.u32, 4); break;
    case GGUF_VAL_INT32:   READ(kv->val.i32, 4); break;
    case GGUF_VAL_UINT64:  READ(kv->val.u64, 8); break;
    case GGUF_VAL_INT64:   READ(kv->val.i64, 8); break;
    case GGUF_VAL_FLOAT32: READ(kv->val.f32, 4); break;
    case GGUF_VAL_FLOAT64: READ(kv->val.f64, 8); break;
    case GGUF_VAL_BOOL:    READ(kv->val.b,   1); break;
    case GGUF_VAL_STRING: {
        /* Store pointer into segment buf (no copy) — valid while seg locked */
        if (*off + 8 > buf_sz) return -1;
        uint64_t slen;
        llm_copy(&slen, buf + *off, 8);
        *off += 8;
        if (*off + slen > buf_sz) return -1;
        kv->val.str.data = (char*)(buf + *off);
        kv->val.str.len  = slen;
        /* Also NUL-terminate if space exists (data points into read-only seg,
         * but for model string metadata this is fine). */
        *off += slen;
        break;
    }
    case GGUF_VAL_ARRAY: {
        uint32_t elem_type;
        uint64_t count;
        READ(elem_type, 4);
        READ(count,     8);
        kv->val.arr.elem_type = (gguf_val_type_t)elem_type;
        kv->val.arr.count     = count;
        kv->val.arr.data      = (void*)(buf + *off); /* pointer into segment */
        /* Skip the raw data bytes */
        /* Each element is variable-width; for now skip string arrays safely */
        if (elem_type == GGUF_VAL_STRING) {
            for (uint64_t i = 0; i < count; i++) {
                uint64_t sl;
                READ(sl, 8);
                if (*off + sl > buf_sz) return -1;
                *off += sl;
            }
        } else {
            static const uint8_t sizes[] = {1,1,2,2,4,4,4,1,0,0,8,8,8};
            uint8_t esz = (elem_type < 13) ? sizes[elem_type] : 4;
            *off += esz * count;
        }
        break;
    }
    default:
        klog_warn("[llm-mmap] unknown GGUF value type");
        return -1;
    }
#undef READ
    return 0;
}

static int parse_gguf_header(gguf_header_t* hdr, const uint8_t* buf,
                              uint64_t buf_sz)
{
    uint64_t off = 0;
    uint32_t magic;

    /* Magic + version */
    if (off + 8 > buf_sz) return -1;
    llm_copy(&magic,       buf + 0, 4);
    llm_copy(&hdr->version,buf + 4, 4);
    off = 8;
    if (magic != GGUF_MAGIC) {
        klog_error("[llm-mmap] bad GGUF magic");
        return -1;
    }
    if (hdr->version < GGUF_VERSION_MIN || hdr->version > GGUF_VERSION_MAX) {
        klog_error("[llm-mmap] unsupported GGUF version");
        return -1;
    }

    /* Tensor & KV counts */
    if (off + 16 > buf_sz) return -1;
    llm_copy(&hdr->n_tensors, buf + off,     8);
    llm_copy(&hdr->n_kv,      buf + off + 8, 8);
    off += 16;

    /* Sanity */
    if (hdr->n_kv > GGUF_MAX_KV || hdr->n_tensors > GGUF_MAX_TENSORS) {
        klog_error("[llm-mmap] too many KV or tensor entries");
        return -1;
    }

    hdr->kv      = hdr->kv_store;
    hdr->tensors = hdr->tensor_store;

    /* Metadata KV pairs */
    for (uint64_t i = 0; i < hdr->n_kv; i++) {
        gguf_kv_t* kv = &hdr->kv[i];
        if (parse_gguf_string(buf, buf_sz, &off, kv->key,
                              GGUF_KV_KEY_MAX) != 0) return -1;
        uint32_t val_type;
        if (off + 4 > buf_sz) return -1;
        llm_copy(&val_type, buf + off, 4);
        off += 4;
        kv->type = (gguf_val_type_t)val_type;
        if (parse_gguf_val(buf, buf_sz, &off, kv->type, kv) != 0)
            return -1;
    }

    /* Tensor info */
    for (uint64_t i = 0; i < hdr->n_tensors; i++) {
        gguf_tensor_info_t* ti = &hdr->tensors[i];
        if (parse_gguf_string(buf, buf_sz, &off, ti->name,
                              GGUF_TENSOR_NAME_MAX) != 0) return -1;
        uint32_t n_dims;
        if (off + 4 > buf_sz) return -1;
        llm_copy(&n_dims, buf + off, 4);
        off += 4;
        ti->n_dims = n_dims;
        for (uint32_t d = 0; d < n_dims && d < GGUF_TENSOR_MAX_DIMS; d++) {
            if (off + 8 > buf_sz) return -1;
            llm_copy(&ti->dims[d], buf + off, 8);
            off += 8;
        }
        if (off + 8 > buf_sz) return -1;
        llm_copy(&ti->type, buf + off, 4);
        off += 4;
        llm_copy(&ti->data_offset, buf + off, 8);
        off += 8;
    }

    /* Alignment padding to 32 bytes before tensor data section */
    off = (off + 31) & ~(uint64_t)31;
    hdr->data_section_offset = off;

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

int llm_mmap_open(llm_mmap_t* mm, const char* path)
{
    llm_zero(mm, sizeof(*mm));

    /* --- Determine file size --- */
    uint64_t file_sz = 0;
    extern struct bfs_instance g_bfs;
    if (bfs_file_size(&g_bfs, path, &file_sz) != 0 || file_sz < 16) {
        klog_error("[llm-mmap] model file not found");
        return -1;
    }

    /* --- Allocate SASY segment for the whole file --- */
    mm->seg = sasy_create(file_sz, SEG_CODE,
                          SEG_FLAG_READ | SEG_FLAG_NOSWAP | SEG_FLAG_EXEC);
    if (mm->seg == INVALID_HANDLE) {
        klog_error("[llm-mmap] SASY segment alloc failed");
        return -1;
    }

    mm->addr = (uint8_t*)sasy_lock(mm->seg);
    if (!mm->addr) {
        sasy_free(mm->seg);
        mm->seg = INVALID_HANDLE;
        klog_error("[llm-mmap] SASY lock failed");
        return -1;
    }

    /* --- Load file into segment --- */
    uint64_t actual = 0;
    if (bfs_read_file(&g_bfs, path, mm->addr, file_sz, &actual) != 0 ||
        actual != file_sz) {
        sasy_unlock(mm->seg);
        sasy_free(mm->seg);
        mm->seg = INVALID_HANDLE;
        klog_error("[llm-mmap] file read error");
        return -1;
    }
    mm->file_size = file_sz;

    /* --- Parse GGUF header --- */
    if (parse_gguf_header(&mm->hdr, mm->addr, file_sz) != 0) {
        sasy_unlock(mm->seg);
        sasy_free(mm->seg);
        mm->seg = INVALID_HANDLE;
        return -1;
    }
    mm->data_offset = mm->hdr.data_section_offset;
    mm->loaded = 1;

    klog_log("[llm-mmap] model loaded");
    return 0;
}

void llm_mmap_close(llm_mmap_t* mm)
{
    if (!mm || !mm->loaded) return;
    if (mm->seg != INVALID_HANDLE) {
        sasy_unlock(mm->seg);
        sasy_free(mm->seg);
        mm->seg = INVALID_HANDLE;
    }
    mm->addr   = (uint8_t*)0;
    mm->loaded = 0;
}

const void* llm_mmap_tensor_data(const llm_mmap_t* mm, const char* name,
                                  uint64_t* out_bytes)
{
    if (!mm || !mm->loaded || !name) return (const void*)0;

    for (uint64_t i = 0; i < mm->hdr.n_tensors; i++) {
        const gguf_tensor_info_t* ti = &mm->hdr.tensors[i];
        if (str_eq(ti->name, name)) {
            /* Compute total element count */
            uint64_t n_elem = 1;
            for (uint32_t d = 0; d < ti->n_dims; d++)
                n_elem *= ti->dims[d];
            /* Byte size from ggml_type_size / blck_size */
            extern size_t   ggml_type_size(int t);
            extern uint32_t ggml_blck_size(int t);
            size_t   bsz  = ggml_type_size(ti->type);
            uint32_t blck = ggml_blck_size(ti->type);
            uint64_t nbytes = (n_elem / blck) * bsz;
            if (out_bytes) *out_bytes = nbytes;
            return mm->addr + mm->data_offset + ti->data_offset;
        }
    }
    return (const void*)0;
}

const gguf_kv_t* llm_mmap_find_kv(const llm_mmap_t* mm, const char* key)
{
    if (!mm || !key) return (const gguf_kv_t*)0;
    for (uint64_t i = 0; i < mm->hdr.n_kv; i++) {
        if (str_eq(mm->hdr.kv[i].key, key))
            return &mm->hdr.kv[i];
    }
    return (const gguf_kv_t*)0;
}

uint32_t llm_mmap_kv_u32(const llm_mmap_t* mm, const char* key,
                           uint32_t default_val)
{
    const gguf_kv_t* kv = llm_mmap_find_kv(mm, key);
    if (!kv) return default_val;
    switch (kv->type) {
    case GGUF_VAL_UINT32: return kv->val.u32;
    case GGUF_VAL_INT32:  return (uint32_t)kv->val.i32;
    case GGUF_VAL_UINT64: return (uint32_t)kv->val.u64;
    default: return default_val;
    }
}

float llm_mmap_kv_f32(const llm_mmap_t* mm, const char* key,
                       float default_val)
{
    const gguf_kv_t* kv = llm_mmap_find_kv(mm, key);
    if (!kv) return default_val;
    if (kv->type == GGUF_VAL_FLOAT32) return kv->val.f32;
    return default_val;
}

int llm_mmap_kv_str(const llm_mmap_t* mm, const char* key,
                     char* buf, size_t buf_max)
{
    const gguf_kv_t* kv = llm_mmap_find_kv(mm, key);
    if (!kv || kv->type != GGUF_VAL_STRING || !kv->val.str.data)
        return -1;
    uint64_t copy = kv->val.str.len < buf_max - 1
                    ? kv->val.str.len : buf_max - 1;
    llm_copy(buf, kv->val.str.data, (size_t)copy);
    buf[copy] = '\0';
    return 0;
}
