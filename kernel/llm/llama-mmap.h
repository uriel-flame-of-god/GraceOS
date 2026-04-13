#ifndef GRACE_LLM_MMAP_H
#define GRACE_LLM_MMAP_H

/*
 * llama-mmap.h — SASY-backed model file mapping
 *
 * Replaces the mmap()/munmap() calls in the original llama.cpp with the
 * GraceOS SASY segment allocator so that model weights live inside tracked
 * virtual memory regions instead of anonymous kernel mappings.
 *
 * The mapping is loaded eagerly when the model is first opened.  The segment
 * carries SEG_FLAG_NOSWAP so the kernel will not evict model weights under
 * normal memory pressure; this flag can be relaxed at runtime.
 */

#include "../../lib/libc/int.h"
#include "../mm/sasy/sasy.h"

/* -------------------------------------------------------------------------
 * GGUF file on-disk structures
 * ------------------------------------------------------------------------- */

#define GGUF_MAGIC          0x46554747u   /* "GGUF" little-endian */
#define GGUF_VERSION_MIN    2
#define GGUF_VERSION_MAX    3

/* GGUF metadata value types */
typedef enum {
    GGUF_VAL_UINT8   = 0,
    GGUF_VAL_INT8    = 1,
    GGUF_VAL_UINT16  = 2,
    GGUF_VAL_INT16   = 3,
    GGUF_VAL_UINT32  = 4,
    GGUF_VAL_INT32   = 5,
    GGUF_VAL_FLOAT32 = 6,
    GGUF_VAL_BOOL    = 7,
    GGUF_VAL_STRING  = 8,
    GGUF_VAL_ARRAY   = 9,
    GGUF_VAL_UINT64  = 10,
    GGUF_VAL_INT64   = 11,
    GGUF_VAL_FLOAT64 = 12,
} gguf_val_type_t;

/* One metadata key-value pair (parsed from the file header) */
#define GGUF_KV_KEY_MAX   128
typedef struct {
    char         key[GGUF_KV_KEY_MAX];
    gguf_val_type_t  type;
    union {
        uint8_t   u8;
        int8_t    i8;
        uint16_t  u16;
        int16_t   i16;
        uint32_t  u32;
        int32_t   i32;
        uint64_t  u64;
        int64_t   i64;
        float     f32;
        double    f64;
        uint8_t   b;
        struct { char* data; uint64_t len; } str;
        struct { gguf_val_type_t elem_type; uint64_t count; void* data; } arr;
    } val;
} gguf_kv_t;

/* One tensor descriptor parsed from the header */
#define GGUF_TENSOR_NAME_MAX  128
#define GGUF_TENSOR_MAX_DIMS  4
typedef struct {
    char     name[GGUF_TENSOR_NAME_MAX];
    uint32_t n_dims;
    uint64_t dims[GGUF_TENSOR_MAX_DIMS];
    uint32_t type;          /* ggml_type_t */
    uint64_t data_offset;   /* byte offset into tensor data section */
} gguf_tensor_info_t;

/* Parsed GGUF file header */
#define GGUF_MAX_KV       512
#define GGUF_MAX_TENSORS  1024
typedef struct {
    uint32_t version;
    uint64_t n_tensors;
    uint64_t n_kv;
    gguf_kv_t*          kv;       /* points into kv_store[] */
    gguf_tensor_info_t* tensors;  /* points into tensor_store[] */
    uint64_t data_section_offset; /* byte offset where tensor data begins */

    /* Fixed storage avoids dynamic allocation for small models */
    gguf_kv_t           kv_store[GGUF_MAX_KV];
    gguf_tensor_info_t  tensor_store[GGUF_MAX_TENSORS];
} gguf_header_t;

/* -------------------------------------------------------------------------
 * LLM mapping object
 * Holds the SASY segment that backs all model tensor data.
 * ------------------------------------------------------------------------- */
typedef struct {
    seg_handle_t  seg;            /* SASY segment (SEG_CODE, read-only shared) */
    uint8_t*      addr;           /* sasy_lock() result — kept locked           */
    uint64_t      file_size;      /* total file size in bytes                   */
    uint64_t      data_offset;    /* offset of tensor data section              */
    gguf_header_t hdr;            /* parsed GGUF header                         */
    int           loaded;         /* 1 if file was fully loaded into segment    */
} llm_mmap_t;

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/*
 * Load the GGUF model at 'path' into a SASY code segment.
 * Parses the header and populates *mm.
 * Returns 0 on success, -1 on failure.
 * The segment remains locked (sasy_lock) until llm_mmap_close().
 */
int llm_mmap_open(llm_mmap_t* mm, const char* path);

/* Release segment, unlock, and zero the struct. */
void llm_mmap_close(llm_mmap_t* mm);

/*
 * Return a pointer to the raw bytes of the named tensor.
 * The returned pointer is valid as long as the mmap is open.
 * Returns NULL if not found.
 */
const void* llm_mmap_tensor_data(const llm_mmap_t* mm, const char* name,
                                 uint64_t* out_bytes);

/* Look up a metadata value by key.  Returns NULL if not found. */
const gguf_kv_t* llm_mmap_find_kv(const llm_mmap_t* mm, const char* key);

/* Helper: read a uint32 metadata value; returns default_val on failure. */
uint32_t llm_mmap_kv_u32(const llm_mmap_t* mm, const char* key,
                          uint32_t default_val);

/* Helper: read a float32 metadata value; returns default_val on failure. */
float llm_mmap_kv_f32(const llm_mmap_t* mm, const char* key,
                      float default_val);

/* Helper: read a string metadata value into buf (NUL-terminated).
 * Returns 0 on success, -1 if key missing or buffer too small. */
int llm_mmap_kv_str(const llm_mmap_t* mm, const char* key,
                    char* buf, size_t buf_max);

#endif /* GRACE_LLM_MMAP_H */
