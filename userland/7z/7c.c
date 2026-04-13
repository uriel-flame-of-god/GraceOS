#include "7c.h"

#include "../../lib/libc/int.h"
#include "../../lib/libc/string.h"
#include "../../lib/libgrace/grace.h"
#include "../../include/grace/spm_syscalls.h"
#include "../../drivers/storage/bfs.h"

extern struct bfs_instance g_bfs;

#define ZIP_SIG_LOCAL   0x04034b50u
#define ZIP_SIG_CENTRAL 0x02014b50u
#define ZIP_SIG_EOCD    0x06054b50u

#define ZIP_METHOD_STORE 0
#define ZIP_BUF_MIN 4096u
#define ZIP_LIST_BUF 4096

#define ZIP_MAX_SEGMENTS 32
#define ZIP_SEGMENT_MAX  48
#define ZIP_PATH_MAX     128

struct zip_buf {
    uint8_t *data;
    size_t size;
    size_t cap;
};

struct zip_entry {
    char *src_path;
    char *zip_name;
    uint32_t size;
    uint32_t crc;
    uint32_t offset;
};

struct zip_list {
    struct zip_entry *items;
    size_t count;
    size_t cap;
};

static void print_line(const char *a, const char *b)
{
    print(a);
    println(b);
}

static char *dup_str(const char *s)
{
    size_t len = strlen(s);
    char *out = (char *)malloc(len + 1);
    if (!out)
        return NULL;
    memcpy(out, s, len + 1);
    return out;
}

static int zip_buf_reserve(struct zip_buf *buf, size_t add)
{
    size_t needed = buf->size + add;
    if (needed <= buf->cap)
        return 1;

    size_t new_cap = buf->cap ? buf->cap : ZIP_BUF_MIN;
    while (new_cap < needed)
    {
        if (new_cap > (size_t)-1 / 2)
        {
            new_cap = needed;
            break;
        }
        new_cap *= 2;
    }

    uint8_t *new_data = (uint8_t *)malloc(new_cap);
    if (!new_data)
        return 0;

    if (buf->data && buf->size)
        memcpy(new_data, buf->data, buf->size);
    if (buf->data)
        free(buf->data);

    buf->data = new_data;
    buf->cap = new_cap;
    return 1;
}

static int zip_buf_append(struct zip_buf *buf, const void *data, size_t len)
{
    if (!zip_buf_reserve(buf, len))
        return 0;
    memcpy(buf->data + buf->size, data, len);
    buf->size += len;
    return 1;
}

static int zip_buf_append_u16(struct zip_buf *buf, uint16_t v)
{
    uint8_t tmp[2];
    tmp[0] = (uint8_t)(v & 0xFF);
    tmp[1] = (uint8_t)((v >> 8) & 0xFF);
    return zip_buf_append(buf, tmp, sizeof(tmp));
}

static int zip_buf_append_u32(struct zip_buf *buf, uint32_t v)
{
    uint8_t tmp[4];
    tmp[0] = (uint8_t)(v & 0xFF);
    tmp[1] = (uint8_t)((v >> 8) & 0xFF);
    tmp[2] = (uint8_t)((v >> 16) & 0xFF);
    tmp[3] = (uint8_t)((v >> 24) & 0xFF);
    return zip_buf_append(buf, tmp, sizeof(tmp));
}

static void zip_list_free(struct zip_list *list)
{
    if (!list || !list->items)
        return;
    for (size_t i = 0; i < list->count; i++)
    {
        free(list->items[i].src_path);
        free(list->items[i].zip_name);
    }
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

static int zip_list_push(struct zip_list *list, const char *src, const char *name, uint32_t size)
{
    if (list->count >= list->cap)
    {
        size_t new_cap = list->cap ? list->cap * 2 : 16;
        struct zip_entry *new_items = (struct zip_entry *)malloc(new_cap * sizeof(*new_items));
        if (!new_items)
            return 0;
        if (list->items && list->count)
            memcpy(new_items, list->items, list->count * sizeof(*new_items));
        if (list->items)
            free(list->items);
        list->items = new_items;
        list->cap = new_cap;
    }

    list->items[list->count].src_path = dup_str(src);
    list->items[list->count].zip_name = dup_str(name);
    if (!list->items[list->count].src_path || !list->items[list->count].zip_name)
    {
        if (list->items[list->count].src_path)
            free(list->items[list->count].src_path);
        if (list->items[list->count].zip_name)
            free(list->items[list->count].zip_name);
        return 0;
    }
    list->items[list->count].size = size;
    list->items[list->count].crc = 0;
    list->items[list->count].offset = 0;
    list->count++;
    return 1;
}

static uint32_t crc32_table[256];
static int crc32_ready = 0;

static void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (uint32_t j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_ready = 1;
}

static uint32_t crc32_calc(const uint8_t *data, size_t len)
{
    if (!crc32_ready)
        crc32_init();

    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++)
        c = crc32_table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)p[0] | (uint16_t)(p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int build_spm_target(const char *path, char *out, size_t out_len)
{
    size_t len = strlen(path);
    if (len + 2 > out_len)
        return 0;
    out[0] = '/';
    memcpy(out + 1, path, len + 1);
    return 1;
}

static int check_overwrite_permission(const char *path)
{
    char target[SPM_TARGET_MAX];
    if (!build_spm_target(path, target, sizeof(target)))
        return 0;
    return spm_check_user((uint32_t)getuid(), PERM_WRITE, target) == 0;
}

static int ensure_dir_path(const char *path)
{
    if (!path || !*path)
        return 1;

    char buf[ZIP_PATH_MAX];
    size_t len = strlen(path);
    if (len >= sizeof(buf))
        return 0;

    memcpy(buf, path, len + 1);

    for (size_t i = 0; i < len; i++)
    {
        if (buf[i] == '/')
        {
            buf[i] = '\0';
            if (buf[0] != '\0')
            {
                int rc = bfs_create(&g_bfs, buf, 0);
                if (rc != 0 && rc != -1)
                    return 0;
            }
            buf[i] = '/';
        }
    }

    if (buf[0] != '\0')
    {
        int rc = bfs_create(&g_bfs, buf, 0);
        if (rc != 0 && rc != -1)
            return 0;
    }

    return 1;
}

static int join_paths(char *out, size_t out_len, const char *a, const char *b)
{
    size_t a_len = a ? strlen(a) : 0;
    size_t b_len = b ? strlen(b) : 0;
    size_t need = a_len + b_len + 2;

    if (need > out_len)
        return 0;

    out[0] = '\0';
    if (a_len)
        memcpy(out, a, a_len);
    if (a_len && out[a_len - 1] != '/')
        out[a_len++] = '/';
    if (b_len)
        memcpy(out + a_len, b, b_len);
    out[a_len + b_len] = '\0';
    return 1;
}

static int normalize_zip_name(const char *in, char *out, size_t out_len, int *is_dir)
{
    char segments[ZIP_MAX_SEGMENTS][ZIP_SEGMENT_MAX];
    int seg_count = 0;

    size_t in_len = strlen(in);
    if (in_len == 0)
        return 0;

    *is_dir = (in[in_len - 1] == '/' || in[in_len - 1] == '\\');

    const char *p = in;
    while (*p)
    {
        while (*p == '/' || *p == '\\')
            p++;
        if (!*p)
            break;

        int seg_len = 0;
        const char *seg_start = p;
        while (*p && *p != '/' && *p != '\\')
        {
            if (seg_len + 1 >= ZIP_SEGMENT_MAX)
                return 0;
            seg_len++;
            p++;
        }

        if (seg_len == 1 && seg_start[0] == '.')
            continue;
        if (seg_len == 2 && seg_start[0] == '.' && seg_start[1] == '.')
        {
            if (seg_count == 0)
                return 0;
            seg_count--;
            continue;
        }

        if (seg_count >= ZIP_MAX_SEGMENTS)
            return 0;

        for (int i = 0; i < seg_len; i++)
            segments[seg_count][i] = seg_start[i];
        segments[seg_count][seg_len] = '\0';
        seg_count++;
    }

    if (seg_count == 0)
        return 0;

    size_t pos = 0;
    for (int i = 0; i < seg_count; i++)
    {
        size_t seg_len = strlen(segments[i]);
        if (pos + seg_len + 2 > out_len)
            return 0;
        if (pos)
            out[pos++] = '/';
        memcpy(out + pos, segments[i], seg_len);
        pos += seg_len;
    }

    out[pos] = '\0';
    return 1;
}

static const char *path_basename(const char *path)
{
    const char *last = path;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            last = p + 1;
    return last;
}

static int path_parent(const char *path, char *out, size_t out_len)
{
    size_t len = strlen(path);
    if (len == 0)
    {
        if (out_len > 0)
            out[0] = '\0';
        return 1;
    }

    const char *last = NULL;
    for (const char *p = path; *p; p++)
        if (*p == '/')
            last = p;

    if (!last)
    {
        if (out_len > 0)
            out[0] = '\0';
        return 1;
    }

    size_t plen = (size_t)(last - path);
    if (plen >= out_len)
        return 0;
    memcpy(out, path, plen);
    out[plen] = '\0';
    return 1;
}

static int collect_dir(const char *base_path, const char *rel_path, struct zip_list *list)
{
    char list_buf[ZIP_LIST_BUF];
    int count = bfs_list_dir(&g_bfs, base_path, list_buf, sizeof(list_buf));
    if (count < 0)
        return 0;

    int pos = 0;
    while (list_buf[pos])
    {
        const char *entry = list_buf + pos;
        size_t len = strlen(entry);
        int is_dir = (len > 0 && entry[len - 1] == '/');
        char name[ZIP_SEGMENT_MAX];

        if (len >= sizeof(name))
            return 0;
        memcpy(name, entry, len);
        name[len] = '\0';
        if (is_dir)
            name[len - 1] = '\0';

        char next_base[ZIP_PATH_MAX];
        char next_rel[ZIP_PATH_MAX];

        if (!join_paths(next_base, sizeof(next_base), base_path, name))
            return 0;

        if (rel_path && rel_path[0])
        {
            if (!join_paths(next_rel, sizeof(next_rel), rel_path, name))
                return 0;
        }
        else
        {
            if (strlen(name) >= sizeof(next_rel))
                return 0;
            memcpy(next_rel, name, strlen(name) + 1);
        }

        if (is_dir)
        {
            if (!collect_dir(next_base, next_rel, list))
                return 0;
        }
        else
        {
            uint64_t size = 0;
            if (bfs_file_size(&g_bfs, next_base, &size) != 0)
                return 0;
            if (size > 0xFFFFFFFFu)
                return 0;
            if (!zip_list_push(list, next_base, next_rel, (uint32_t)size))
                return 0;
        }

        pos += (int)len + 1;
    }

    return 1;
}

static int sevenzip_encode(const char *zip_path, const char *src_path, int overwrite, int verbose)
{
    struct zip_list list = {0};

    uint64_t size = 0;
    int exists = (bfs_file_size(&g_bfs, src_path, &size) == 0);
    if (!exists)
    {
        print_line("7c: input not found: ", src_path);
        return 1;
    }

    if (size == 0)
    {
        char list_buf[ZIP_LIST_BUF];
        int list_count = bfs_list_dir(&g_bfs, src_path, list_buf, sizeof(list_buf));
        if (list_count < 0)
        {
            println("7c: failed to read directory");
            zip_list_free(&list);
            return 1;
        }

        if (list_count > 0)
        {
            if (!collect_dir(src_path, "", &list))
            {
                println("7c: failed to list directory");
                zip_list_free(&list);
                return 1;
            }
        }
        else
        {
            const char *base = path_basename(src_path);
            if (!zip_list_push(&list, src_path, base, 0))
            {
                println("7c: out of memory");
                zip_list_free(&list);
                return 1;
            }
        }
    }
    else
    {
        const char *base = path_basename(src_path);
        if (!zip_list_push(&list, src_path, base, (uint32_t)size))
        {
            println("7c: out of memory");
            zip_list_free(&list);
            return 1;
        }
    }

    struct bfs_file_entry entry;
    if (bfs_find(&g_bfs, zip_path, &entry) == 0)
    {
        if (!overwrite)
        {
            print_line("7c: output exists: ", zip_path);
            zip_list_free(&list);
            return 1;
        }
        if (!check_overwrite_permission(zip_path))
        {
            print_line("7c: overwrite denied: ", zip_path);
            zip_list_free(&list);
            return 1;
        }
    }

    struct zip_buf buf = {0};

    for (size_t i = 0; i < list.count; i++)
    {
        struct zip_entry *ent = &list.items[i];
        if (verbose)
            print_line("add ", ent->zip_name);

        uint8_t *file_data = NULL;
        uint64_t out_size = 0;

        if (ent->size > 0)
        {
            file_data = (uint8_t *)malloc(ent->size);
            if (!file_data)
            {
                println("7c: out of memory");
                zip_list_free(&list);
                return 1;
            }

            if (bfs_read_file(&g_bfs, ent->src_path, file_data, ent->size, &out_size) != 0 || out_size != ent->size)
            {
                free(file_data);
                zip_list_free(&list);
                print_line("7c: failed to read ", ent->src_path);
                return 1;
            }
        }

        ent->crc = crc32_calc(file_data, ent->size);
        ent->offset = (uint32_t)buf.size;

        if (!zip_buf_append_u32(&buf, ZIP_SIG_LOCAL) ||
            !zip_buf_append_u16(&buf, 20) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append_u16(&buf, ZIP_METHOD_STORE) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append_u32(&buf, ent->crc) ||
            !zip_buf_append_u32(&buf, ent->size) ||
            !zip_buf_append_u32(&buf, ent->size))
        {
            free(file_data);
            zip_list_free(&list);
            println("7c: failed to build zip");
            return 1;
        }

        uint16_t name_len = (uint16_t)strlen(ent->zip_name);
        if (!zip_buf_append_u16(&buf, name_len) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append(&buf, ent->zip_name, name_len) ||
            (ent->size && !zip_buf_append(&buf, file_data, ent->size)))
        {
            free(file_data);
            zip_list_free(&list);
            println("7c: failed to build zip");
            return 1;
        }

        if (file_data)
            free(file_data);
    }

    uint32_t cd_offset = (uint32_t)buf.size;
    for (size_t i = 0; i < list.count; i++)
    {
        struct zip_entry *ent = &list.items[i];
        uint16_t name_len = (uint16_t)strlen(ent->zip_name);

        if (!zip_buf_append_u32(&buf, ZIP_SIG_CENTRAL) ||
            !zip_buf_append_u16(&buf, 20) ||
            !zip_buf_append_u16(&buf, 20) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append_u16(&buf, ZIP_METHOD_STORE) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append_u32(&buf, ent->crc) ||
            !zip_buf_append_u32(&buf, ent->size) ||
            !zip_buf_append_u32(&buf, ent->size) ||
            !zip_buf_append_u16(&buf, name_len) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append_u16(&buf, 0) ||
            !zip_buf_append_u32(&buf, 0) ||
            !zip_buf_append_u32(&buf, ent->offset) ||
            !zip_buf_append(&buf, ent->zip_name, name_len))
        {
            zip_list_free(&list);
            println("7c: failed to build zip");
            return 1;
        }
    }

    uint32_t cd_size = (uint32_t)(buf.size - cd_offset);
    uint16_t total_entries = (uint16_t)list.count;

    if (!zip_buf_append_u32(&buf, ZIP_SIG_EOCD) ||
        !zip_buf_append_u16(&buf, 0) ||
        !zip_buf_append_u16(&buf, 0) ||
        !zip_buf_append_u16(&buf, total_entries) ||
        !zip_buf_append_u16(&buf, total_entries) ||
        !zip_buf_append_u32(&buf, cd_size) ||
        !zip_buf_append_u32(&buf, cd_offset) ||
        !zip_buf_append_u16(&buf, 0))
    {
        zip_list_free(&list);
        println("7c: failed to finalize zip");
        return 1;
    }

    if (bfs_write_file(&g_bfs, zip_path, buf.data, buf.size) != 0)
    {
        zip_list_free(&list);
        println("7c: write failed");
        return 1;
    }

    zip_list_free(&list);
    if (buf.data)
        free(buf.data);

    println("7c: encode complete");
    return 0;
}

static int sevenzip_decode(const char *zip_path, const char *out_dir, int overwrite, int verbose)
{
    uint64_t zip_size = 0;
    if (bfs_file_size(&g_bfs, zip_path, &zip_size) != 0)
    {
        print_line("7c: input not found: ", zip_path);
        return 1;
    }

    if (zip_size > (uint64_t)(size_t)-1)
    {
        println("7c: zip too large");
        return 1;
    }

    uint8_t *zip_data = (uint8_t *)malloc((size_t)zip_size);
    if (!zip_data)
    {
        println("7c: out of memory");
        return 1;
    }

    uint64_t out_size = 0;
    if (bfs_read_file(&g_bfs, zip_path, zip_data, zip_size, &out_size) != 0 || out_size != zip_size)
    {
        free(zip_data);
        println("7c: failed to read zip");
        return 1;
    }

    if (!ensure_dir_path(out_dir))
    {
        free(zip_data);
        println("7c: failed to create output directory");
        return 1;
    }

    size_t pos = 0;
    while (pos + 4 <= (size_t)zip_size)
    {
        uint32_t sig = read_u32(zip_data + pos);
        if (sig != ZIP_SIG_LOCAL)
            break;

        if (pos + 30 > (size_t)zip_size)
            break;

        uint16_t flags = read_u16(zip_data + pos + 6);
        uint16_t method = read_u16(zip_data + pos + 8);
        uint32_t comp_size = read_u32(zip_data + pos + 18);
        uint32_t uncomp_size = read_u32(zip_data + pos + 22);
        uint16_t name_len = read_u16(zip_data + pos + 26);
        uint16_t extra_len = read_u16(zip_data + pos + 28);

        if (flags & 0x08)
        {
            println("7c: data descriptors not supported");
            free(zip_data);
            return 1;
        }

        if (method != ZIP_METHOD_STORE)
        {
            println("7c: unsupported compression method");
            free(zip_data);
            return 1;
        }

        size_t header_size = 30 + name_len + extra_len;
        if (pos + header_size > (size_t)zip_size)
            break;

        const char *name_ptr = (const char *)(zip_data + pos + 30);
        char name_buf[ZIP_PATH_MAX];
        if (name_len >= sizeof(name_buf))
        {
            println("7c: name too long");
            free(zip_data);
            return 1;
        }
        memcpy(name_buf, name_ptr, name_len);
        name_buf[name_len] = '\0';

        int is_dir = 0;
        char norm_name[ZIP_PATH_MAX];
        if (!normalize_zip_name(name_buf, norm_name, sizeof(norm_name), &is_dir))
        {
            println("7c: invalid path in zip");
            free(zip_data);
            return 1;
        }

        char out_path[ZIP_PATH_MAX];
        if (!join_paths(out_path, sizeof(out_path), out_dir, norm_name))
        {
            println("7c: output path too long");
            free(zip_data);
            return 1;
        }

        size_t data_pos = pos + header_size;
        if (data_pos + comp_size > (size_t)zip_size)
            break;

        if (is_dir)
        {
            if (!ensure_dir_path(out_path))
            {
                free(zip_data);
                println("7c: failed to create directory");
                return 1;
            }
        }
        else
        {
            char parent[ZIP_PATH_MAX];
            if (!path_parent(out_path, parent, sizeof(parent)))
            {
                free(zip_data);
                println("7c: failed to create directory");
                return 1;
            }

            if (parent[0] != '\0' && !ensure_dir_path(parent))
            {
                free(zip_data);
                println("7c: failed to create directory");
                return 1;
            }

            struct bfs_file_entry ent;
            if (bfs_find(&g_bfs, out_path, &ent) == 0)
            {
                if (!overwrite)
                {
                    if (verbose)
                        print_line("skip ", out_path);
                    pos = data_pos + comp_size;
                    continue;
                }

                if (!check_overwrite_permission(out_path))
                {
                    print_line("7c: overwrite denied: ", out_path);
                    free(zip_data);
                    return 1;
                }
            }

            if (uncomp_size != comp_size)
            {
                println("7c: size mismatch");
                free(zip_data);
                return 1;
            }

            if (bfs_write_file(&g_bfs, out_path, zip_data + data_pos, comp_size) != 0)
            {
                print_line("7c: write failed: ", out_path);
                free(zip_data);
                return 1;
            }

            if (verbose)
                print_line("extract ", out_path);
        }

        pos = data_pos + comp_size;
    }

    free(zip_data);
    println("7c: decode complete");
    return 0;
}

static void sevenzip_usage(void)
{
    println("Usage: 7c encode <zip> <src> [--overwrite] [--verbose]");
    println("       7c decode <zip> [outdir] [--overwrite] [--verbose]");
}

int sevenzip_main(int argc, char **argv)
{
    if (argc < 2)
    {
        sevenzip_usage();
        return 1;
    }

    const char *mode = argv[1];
    const char *zip_path = NULL;
    const char *arg_path = NULL;
    const char *out_dir = NULL;
    int overwrite = 0;
    int verbose = 0;

    for (int i = 2; i < argc; i++)
    {
        const char *arg = argv[i];
        if (!arg || !*arg)
            continue;

        if (strcmp(arg, "--overwrite") == 0)
        {
            overwrite = 1;
            continue;
        }
        if (strcmp(arg, "--verbose") == 0)
        {
            verbose = 1;
            continue;
        }
        if (strcmp(arg, "--help") == 0)
        {
            sevenzip_usage();
            return 0;
        }

        if (!zip_path)
            zip_path = arg;
        else if (strcmp(mode, "encode") == 0 && !arg_path)
            arg_path = arg;
        else if (strcmp(mode, "decode") == 0 && !out_dir)
            out_dir = arg;
        else
        {
            println("7c: unexpected argument");
            return 1;
        }
    }

    if (!zip_path)
    {
        sevenzip_usage();
        return 1;
    }

    while (*zip_path == '/')
        zip_path++;
    if (*zip_path == '\0')
    {
        sevenzip_usage();
        return 1;
    }

    if (strcmp(mode, "encode") == 0)
    {
        if (!arg_path)
        {
            sevenzip_usage();
            return 1;
        }

        while (*arg_path == '/')
            arg_path++;
        if (*arg_path == '\0')
        {
            sevenzip_usage();
            return 1;
        }

        return sevenzip_encode(zip_path, arg_path, overwrite, verbose);
    }

    if (strcmp(mode, "decode") == 0)
    {
        if (!out_dir)
            out_dir = "result";

        while (*out_dir == '/')
            out_dir++;
        if (*out_dir == '\0')
        {
            sevenzip_usage();
            return 1;
        }

        return sevenzip_decode(zip_path, out_dir, overwrite, verbose);
    }

    sevenzip_usage();
    return 1;
}
