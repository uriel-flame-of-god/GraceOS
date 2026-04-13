// ============================
// BranchFS-BT Implementation
// Phase 1: Minimal B+ Tree Filesystem
// ============================

#include "bfs.h"
#include "pmem.h"
#include "../../lib/libc/string.h"
#include "../video/tty.h"
#include "../video/serial.h"

// Simple hash function (DJB2)
uint64_t bfs_hash(const char* str)
{
    uint64_t hash = 5381;
    int c;
    
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    
    return hash;
}

static int bfs_device_read(struct bfs_instance* bfs, uint64_t offset, void* buffer, uint64_t size)
{
    if (!bfs || !buffer || size == 0)
        return -1;

    if (bfs->backend == BFS_BACKEND_RAM)
    {
        memcpy(buffer, (void*)(bfs->ram_base + offset), size);
        return 0;
    }

    return pmem_read(offset, buffer, size);
}

static int bfs_device_write(struct bfs_instance* bfs, uint64_t offset, const void* buffer, uint64_t size)
{
    if (!bfs || !buffer || size == 0)
        return -1;

    if (bfs->backend == BFS_BACKEND_RAM)
    {
        memcpy((void*)(bfs->ram_base + offset), buffer, size);
        return 0;
    }

    return pmem_write(offset, buffer, size);
}

static void bfs_flush_root(struct bfs_instance* bfs)
{
    (void)bfs_device_write(bfs, 0, bfs->root_page, BFS_PAGE_SIZE);
}

#define BFS_NAME_ENTRIES (BFS_PAGE_SIZE / sizeof(struct bfs_name_entry))

/* Static buffer for name table to avoid stack overflow */
static struct bfs_name_entry g_name_table_buf[BFS_NAME_ENTRIES];

static int bfs_name_table_read(struct bfs_instance* bfs, struct bfs_name_entry* table)
{
    if (!bfs || !table || !bfs->name_table_enabled)
        return -1;

    uint64_t offset = BFS_NAME_TABLE_PAGE * BFS_PAGE_SIZE;
    return bfs_device_read(bfs, offset, table, BFS_PAGE_SIZE);
}

static int bfs_name_table_write(struct bfs_instance* bfs, const struct bfs_name_entry* table)
{
    if (!bfs || !table || !bfs->name_table_enabled)
        return -1;

    uint64_t offset = BFS_NAME_TABLE_PAGE * BFS_PAGE_SIZE;
    return bfs_device_write(bfs, offset, table, BFS_PAGE_SIZE);
}

static int bfs_name_table_upsert(struct bfs_instance* bfs, uint64_t hash, const char* name, uint64_t start_block, uint64_t size_bytes)
{
    if (!bfs || !name || !bfs->name_table_enabled)
        return 0;

    if (bfs_name_table_read(bfs, g_name_table_buf) != 0)
        return -1;

    int free_index = -1;
    for (int i = 0; i < (int)BFS_NAME_ENTRIES; i++)
    {
        if (g_name_table_buf[i].hash == hash)
        {
            free_index = i;
            break;
        }
        if (free_index < 0 && g_name_table_buf[i].hash == 0)
            free_index = i;
    }

    if (free_index < 0)
        return -1;
    
    g_name_table_buf[free_index].hash = hash;
    g_name_table_buf[free_index].start_block = start_block;
    g_name_table_buf[free_index].size_bytes = size_bytes;

    int i = 0;
    for (; i < BFS_NAME_MAX - 1 && name[i]; i++)
        g_name_table_buf[free_index].name[i] = name[i];
    g_name_table_buf[free_index].name[i] = '\0';

    return bfs_name_table_write(bfs, g_name_table_buf);
}

static void bfs_name_table_remove(struct bfs_instance* bfs, uint64_t hash)
{
    if (!bfs || !bfs->name_table_enabled)
        return;

    if (bfs_name_table_read(bfs, g_name_table_buf) != 0)
        return;

    for (int i = 0; i < (int)BFS_NAME_ENTRIES; i++)
    {
        if (g_name_table_buf[i].hash == hash)
        {
            g_name_table_buf[i].hash = 0;
            g_name_table_buf[i].start_block = 0;
            g_name_table_buf[i].size_bytes = 0;
            g_name_table_buf[i].name[0] = '\0';
            break;
        }
    }

    (void)bfs_name_table_write(bfs, g_name_table_buf);
}

// Read page from disk
struct bfs_page* bfs_read_page(struct bfs_instance* bfs, uint64_t page_num)
{
    if (page_num >= bfs->root->total_pages)
    {
        serial_write("[BFS] ERROR: page ");
        serial_hex(page_num);
        serial_write(" >= total ");
        serial_hex(bfs->root->total_pages);
        serial_write("\n");
        return 0;
    }
    
    uint64_t offset = page_num * BFS_PAGE_SIZE;

    if (bfs_device_read(bfs, offset, bfs->page_buf, BFS_PAGE_SIZE) != 0)
        return 0;

    bfs->page_buf_num = page_num;
    return (struct bfs_page*)bfs->page_buf;
}

// Write page to disk
void bfs_write_page(struct bfs_instance* bfs, uint64_t page_num, struct bfs_page* page)
{
    if (!bfs || !page)
        return;

    uint64_t offset = page_num * BFS_PAGE_SIZE;
    (void)bfs_device_write(bfs, offset, page, BFS_PAGE_SIZE);
}

// Allocate a new page
uint64_t bfs_alloc_page(struct bfs_instance* bfs)
{
    if (bfs->root->free_pages == 0)
        return 0;
    
    uint64_t page_num = bfs->root->next_free;
    bfs->root->next_free++;
    bfs->root->free_pages--;

    bfs_flush_root(bfs);
    
    // Zero out the page
    struct bfs_page* page = (struct bfs_page*)bfs->page_buf;
    memset(page, 0, BFS_PAGE_SIZE);
    page->magic = BFS_MAGIC;
    bfs_write_page(bfs, page_num, page);
    
    return page_num;
}

// Allocate contiguous pages
uint64_t bfs_alloc_pages(struct bfs_instance* bfs, uint64_t pages)
{
    if (!pages || bfs->root->free_pages < pages)
        return 0;

    uint64_t start_page = bfs->root->next_free;
    bfs->root->next_free += pages;
    bfs->root->free_pages -= pages;

    bfs_flush_root(bfs);

    for (uint64_t i = 0; i < pages; i++)
    {
        struct bfs_page* page = (struct bfs_page*)bfs->page_buf;
        memset(page, 0, BFS_PAGE_SIZE);
        page->magic = BFS_MAGIC;
        bfs_write_page(bfs, start_page + i, page);
    }

    return start_page;
}

// Free a page
void bfs_free_page(struct bfs_instance* bfs, uint64_t page_num)
{
    // Simple free list (just mark as reusable)
    bfs->root->free_pages++;
    bfs_flush_root(bfs);
    (void)page_num;
}

// Initialize filesystem
int bfs_init(struct bfs_instance* bfs, uint64_t disk_size, void* ram_base)
{
    if (!bfs || disk_size < (2 * BFS_PAGE_SIZE))
        return -1;

    bfs->disk_size = disk_size;
    bfs->ram_base = (uint8_t*)ram_base;
    bfs->backend = ram_base ? BFS_BACKEND_RAM : BFS_BACKEND_PMEM;
    bfs->page_buf_num = (uint64_t)-1;
    bfs->name_table_enabled = 0;

    if (bfs->backend == BFS_BACKEND_PMEM)
    {
        if (!pmem_ready())
            return -1;

        if (disk_size > pmem_size())
            return -1;
    }

    bfs->root = (struct bfs_root*)bfs->root_page;
    if (bfs_device_read(bfs, 0, bfs->root_page, BFS_PAGE_SIZE) != 0)
        return -1;
    
    // Check if already formatted
    if (bfs->root->magic == BFS_MAGIC)
    {
        bfs->name_table_enabled = (bfs->root->version >= BFS_VERSION);
        return 0;  // Already initialized
    }

    return -1;  // Need to format
}

// Format disk with BranchFS
int bfs_format(struct bfs_instance* bfs)
{
    if (bfs->backend == BFS_BACKEND_RAM && (((uint64_t)bfs->ram_base & 7) != 0))
    {
        serial_write("[BFS] ERROR: disk base not 8-byte aligned\n");
        return -1;
    }

    volatile uint8_t* mem = (volatile uint8_t*)bfs->root_page;
    for (uint64_t i = 0; i < BFS_PAGE_SIZE; i++)
        mem[i] = 0;

    bfs->root->magic       = BFS_MAGIC;
    bfs->root->version     = BFS_VERSION;
    bfs->root->total_pages = bfs->disk_size / BFS_PAGE_SIZE;
    bfs->root->free_pages  = bfs->root->total_pages - 3;
    bfs->root->next_free   = 3;
    bfs->root->file_count  = 0;
    bfs->root->root_page   = 1;
    bfs->name_table_enabled = 1;
    bfs_flush_root(bfs);

    struct bfs_page* root_node = (struct bfs_page*)bfs->page_buf;
    volatile uint8_t* node_mem = (volatile uint8_t*)root_node;
    for (uint64_t i = 0; i < BFS_PAGE_SIZE; i++)
        node_mem[i] = 0;

    root_node->magic  = BFS_MAGIC;
    root_node->level  = 0;
    root_node->count  = 0;
    root_node->next   = 0;
    root_node->parent = 0;
    bfs_write_page(bfs, 1, root_node);

    memset(g_name_table_buf, 0, sizeof(g_name_table_buf));
    (void)bfs_name_table_write(bfs, g_name_table_buf);

    serial_write("[BFS] format complete\n");
    return 0;
}

// Search for key in tree
struct bfs_search_result bfs_search(struct bfs_instance* bfs, uint64_t key)
{
    struct bfs_search_result result;
    result.found = 0;
    result.page_num = 0;
    result.index = -1;
    result.value = 0;
    
    uint64_t current_page_num = bfs->root->root_page;
    struct bfs_page* current = bfs_read_page(bfs, current_page_num);
    
    if (!current)
        return result;
    
    // Traverse tree
    while (current->level > 0)
    {
        // Internal node - find child to follow
        int i;
        for (i = 0; i < current->count; i++)
        {
            if (key < current->keys[i])
                break;
        }
        
        // Go to child
        if (i == 0 && current->count > 0 && key < current->keys[0])
            current_page_num = current->values[0];
        else if (i > 0)
            current_page_num = current->values[i];
        else
            current_page_num = current->values[current->count];
        
        current = bfs_read_page(bfs, current_page_num);
        if (!current)
            return result;
    }
    
    // Leaf node - search for key
    for (int i = 0; i < current->count; i++)
    {
        if (current->keys[i] == key)
        {
            result.found = 1;
            result.page_num = current_page_num;
            result.index = i;
            result.value = current->values[i];
            return result;
        }
    }
    
    // Not found - return position for insertion
    result.page_num = current_page_num;
    for (int i = 0; i < current->count; i++)
    {
        if (key < current->keys[i])
        {
            result.index = i;
            return result;
        }
    }
    result.index = current->count;
    
    return result;
}

// Insert key at position in leaf node
static void insert_in_leaf(struct bfs_page* leaf, int pos, uint64_t key, uint64_t value)
{
    // Shift elements right
    for (int i = leaf->count; i > pos; i--)
    {
        leaf->keys[i] = leaf->keys[i-1];
        leaf->values[i] = leaf->values[i-1];
    }
    
    // Insert new key-value
    leaf->keys[pos] = key;
    leaf->values[pos] = value;
    leaf->count++;
}

/* ============================
   B+ Tree Splitting Support
   ============================ */

/*
 * Cap internal-node keys so that values[count] (the N+1-th child) stays
 * within the values[] array bounds (values[0..BFS_MAX_VALUES-1]).
 */
#define BFS_INTERNAL_MAX_KEYS (BFS_MAX_KEYS - 1)

/* Static scratch buffers so splits never need large stack frames. */
static uint8_t g_split_buf_a[BFS_PAGE_SIZE];
static uint8_t g_split_buf_b[BFS_PAGE_SIZE];

/* Read a page directly into caller-provided buffer (bypasses page_buf). */
static int bfs_raw_read(struct bfs_instance* bfs, uint64_t page_num, void* buf)
{
    return bfs_device_read(bfs, page_num * BFS_PAGE_SIZE, buf, BFS_PAGE_SIZE);
}

/* Write a page directly from caller-provided buffer (bypasses page_buf). */
static void bfs_raw_write(struct bfs_instance* bfs, uint64_t page_num, const void* buf)
{
    bfs_device_write(bfs, page_num * BFS_PAGE_SIZE, buf, BFS_PAGE_SIZE);
}

/* Find the insertion position in a sorted key array. */
static int bfs_key_pos(const uint64_t* keys, int count, uint64_t key)
{
    for (int i = 0; i < count; i++)
        if (key < keys[i])
            return i;
    return count;
}

/*
 * Push a separator key and a new right child up into the parent of
 * `left_page`.  Handles root creation and recursive parent splits.
 * Returns 0 on success.
 */
static int bfs_insert_in_parent(struct bfs_instance* bfs,
                                uint64_t left_page,
                                uint64_t separator,
                                uint64_t right_page)
{
    /* Read left child to discover its parent. */
    if (bfs_raw_read(bfs, left_page, g_split_buf_a) != 0)
        return -1;
    struct bfs_page* lc = (struct bfs_page*)g_split_buf_a;
    uint64_t par_page = lc->parent;

    if (par_page == 0)
    {
        /* left_page was the root – create a brand-new root. */
        uint64_t new_root = bfs_alloc_page(bfs);
        if (!new_root)
            return -1;

        /* Build new root in g_split_buf_b. */
        memset(g_split_buf_b, 0, BFS_PAGE_SIZE);
        struct bfs_page* nr = (struct bfs_page*)g_split_buf_b;
        nr->magic     = BFS_MAGIC;
        nr->level     = lc->level + 1;
        nr->count     = 1;
        nr->keys[0]   = separator;
        nr->values[0] = left_page;
        nr->values[1] = right_page;
        bfs_raw_write(bfs, new_root, nr);

        /* Update parent pointers in both children. */
        if (bfs_raw_read(bfs, left_page, g_split_buf_a) == 0)
        {
            ((struct bfs_page*)g_split_buf_a)->parent = new_root;
            bfs_raw_write(bfs, left_page, g_split_buf_a);
        }
        if (bfs_raw_read(bfs, right_page, g_split_buf_a) == 0)
        {
            ((struct bfs_page*)g_split_buf_a)->parent = new_root;
            bfs_raw_write(bfs, right_page, g_split_buf_a);
        }

        bfs->root->root_page = new_root;
        bfs_flush_root(bfs);
        return 0;
    }

    /* Load the parent internal node (into g_split_buf_b). */
    if (bfs_raw_read(bfs, par_page, g_split_buf_b) != 0)
        return -1;
    struct bfs_page* par = (struct bfs_page*)g_split_buf_b;

    if (par->count < BFS_INTERNAL_MAX_KEYS)
    {
        /* Room in parent – simple sorted insertion. */
        int pos = bfs_key_pos(par->keys, par->count, separator);

        for (int i = par->count; i > pos; i--)
            par->keys[i] = par->keys[i - 1];
        for (int i = par->count + 1; i > pos + 1; i--)
            par->values[i] = par->values[i - 1];

        par->keys[pos]     = separator;
        par->values[pos + 1] = right_page;
        par->count++;
        bfs_raw_write(bfs, par_page, par);

        /* Fix right child's parent pointer. */
        if (bfs_raw_read(bfs, right_page, g_split_buf_a) == 0)
        {
            ((struct bfs_page*)g_split_buf_a)->parent = par_page;
            bfs_raw_write(bfs, right_page, g_split_buf_a);
        }
        return 0;
    }

    /* Parent is full – split the internal node.
     * Build a merged (count+1)-key array on the stack. */
    int  total    = par->count + 1;
    uint64_t tmp_keys[BFS_MAX_KEYS + 1];
    uint64_t tmp_vals[BFS_MAX_KEYS + 2];

    int pos = bfs_key_pos(par->keys, par->count, separator);

    for (int i = 0; i < pos; i++)
    {
        tmp_keys[i] = par->keys[i];
        tmp_vals[i] = par->values[i];
    }
    tmp_keys[pos]     = separator;
    tmp_vals[pos]     = par->values[pos];
    tmp_vals[pos + 1] = right_page;
    for (int i = pos; i < par->count; i++)
    {
        tmp_keys[i + 1] = par->keys[i];
        tmp_vals[i + 2] = par->values[i + 1];
    }

    /* Median key is pushed up (not duplicated, unlike leaf splits). */
    int      mid      = total / 2;
    uint64_t promoted = tmp_keys[mid];

    /* Write left half back to par_page. */
    par->count = mid;
    for (int i = 0; i < mid; i++)
    {
        par->keys[i]   = tmp_keys[i];
        par->values[i] = tmp_vals[i];
    }
    par->values[mid] = tmp_vals[mid];
    bfs_raw_write(bfs, par_page, par);   /* g_split_buf_b done – may reuse */

    /* Allocate and build the right internal node. */
    uint64_t new_right = bfs_alloc_page(bfs);     /* may clobber page_buf, not our statics */
    if (!new_right)
        return -1;

    memset(g_split_buf_a, 0, BFS_PAGE_SIZE);
    struct bfs_page* rp = (struct bfs_page*)g_split_buf_a;
    rp->magic  = BFS_MAGIC;
    rp->level  = par->level;             /* par->level still valid in g_split_buf_b */
    rp->count  = total - mid - 1;
    rp->parent = 0;                       /* set when we recurse */

    for (int i = 0; i < rp->count; i++)
    {
        rp->keys[i]   = tmp_keys[mid + 1 + i];
        rp->values[i] = tmp_vals[mid + 1 + i];
    }
    rp->values[rp->count] = tmp_vals[total];
    bfs_raw_write(bfs, new_right, rp);

    /* Fix parent pointers of all children claimed by new_right. */
    for (int i = 0; i <= rp->count; i++)
    {
        uint64_t child_p = rp->values[i];
        if (bfs_raw_read(bfs, child_p, g_split_buf_b) == 0)
        {
            ((struct bfs_page*)g_split_buf_b)->parent = new_right;
            bfs_raw_write(bfs, child_p, g_split_buf_b);
        }
    }

    /* Push the median key further up the tree. */
    return bfs_insert_in_parent(bfs, par_page, promoted, new_right);
}

// Insert key-value pair with full B+ tree splitting support
int bfs_insert(struct bfs_instance* bfs, uint64_t key, uint64_t value)
{
    struct bfs_search_result search = bfs_search(bfs, key);
    
    // Key already exists - update value
    if (search.found)
    {
        struct bfs_page* leaf = bfs_read_page(bfs, search.page_num);
        leaf->values[search.index] = value;
        bfs_write_page(bfs, search.page_num, leaf);
        return 0;
    }
    
    // Read target leaf into g_split_buf_a (separate from page_buf)
    if (bfs_raw_read(bfs, search.page_num, g_split_buf_a) != 0)
        return -1;
    struct bfs_page* leaf = (struct bfs_page*)g_split_buf_a;

    if (leaf->count < BFS_MAX_KEYS)
    {
        // Simple insertion – leaf has room
        insert_in_leaf(leaf, search.index, key, value);
        bfs_raw_write(bfs, search.page_num, leaf);
        return 0;
    }

    /* Leaf is full – build a temp sorted array of (BFS_MAX_KEYS+1) entries. */
    uint64_t tmp_keys[BFS_MAX_KEYS + 1];
    uint64_t tmp_vals[BFS_MAX_KEYS + 1];

    int ins = search.index;
    for (int i = 0; i < ins; i++)
    {
        tmp_keys[i] = leaf->keys[i];
        tmp_vals[i] = leaf->values[i];
    }
    tmp_keys[ins] = key;
    tmp_vals[ins] = value;
    for (int i = ins; i < (int)leaf->count; i++)
    {
        tmp_keys[i + 1] = leaf->keys[i];
        tmp_vals[i + 1] = leaf->values[i];
    }

    int      total   = (int)leaf->count + 1;
    int      mid     = total / 2;
    uint64_t par_ptr = leaf->parent;
    uint64_t old_nxt = leaf->next;
    uint16_t lvl     = leaf->level;

    /* Left leaf keeps entries [0 .. mid-1]. */
    leaf->count = (uint16_t)mid;
    for (int i = 0; i < mid; i++)
    {
        leaf->keys[i]   = tmp_keys[i];
        leaf->values[i] = tmp_vals[i];
    }

    /* Allocate the right leaf page (may clobber page_buf, not g_split_buf_a). */
    uint64_t right_page = bfs_alloc_page(bfs);
    if (!right_page)
        return -1;

    /* Build the right leaf in g_split_buf_b. */
    memset(g_split_buf_b, 0, BFS_PAGE_SIZE);
    struct bfs_page* right_leaf = (struct bfs_page*)g_split_buf_b;
    right_leaf->magic  = BFS_MAGIC;
    right_leaf->level  = lvl;
    right_leaf->count  = (uint16_t)(total - mid);
    right_leaf->next   = old_nxt;
    right_leaf->parent = par_ptr;

    for (int i = 0; i < right_leaf->count; i++)
    {
        right_leaf->keys[i]   = tmp_keys[mid + i];
        right_leaf->values[i] = tmp_vals[mid + i];
    }

    /* chain: left -> right -> old_next */
    leaf->next = right_page;

    /* Flush both leaves to backing store. */
    bfs_raw_write(bfs, search.page_num, g_split_buf_a);
    bfs_raw_write(bfs, right_page,      g_split_buf_b);

    /* Push the first key of the right leaf up into the parent. */
    return bfs_insert_in_parent(bfs, search.page_num, right_leaf->keys[0], right_page);
}

// Create a file
int bfs_create(struct bfs_instance* bfs, const char* filename, uint64_t size)
{
    uint64_t hash = bfs_hash(filename);
    struct bfs_search_result search = bfs_search(bfs, hash);
    if (search.found)
        return -1;

    uint64_t blocks_needed = (size + BFS_PAGE_SIZE - 1) / BFS_PAGE_SIZE;
    if (blocks_needed > bfs->root->free_pages)
        return -2;

    uint64_t start_block = blocks_needed ? bfs_alloc_pages(bfs, blocks_needed) : 0;
    if (blocks_needed && start_block == 0)
        return -2;

    uint64_t value = (start_block << 32) | (size & 0xFFFFFFFF);
    if (bfs_insert(bfs, hash, value) != 0)
    {
        serial_write("[BFS] insert failed for: ");
        serial_write(filename);
        serial_write("\n");
        return -3;
    }

    bfs->root->file_count++;
    bfs_flush_root(bfs);
    (void)bfs_name_table_upsert(bfs, hash, filename, start_block, size);
    return 0;
}

// Find a file
int bfs_find(struct bfs_instance* bfs, const char* filename, struct bfs_file_entry* entry)
{
    uint64_t hash = bfs_hash(filename);
    struct bfs_search_result search = bfs_search(bfs, hash);
    
    if (!search.found)
        return -1;
    
    // Decode value
    entry->start_block = (search.value >> 32) & 0xFFFFFFFF;
    entry->size_bytes = search.value & 0xFFFFFFFF;
    entry->inode = hash;
    entry->flags = 0;
    
    return 0;
}

int bfs_file_size(struct bfs_instance* bfs, const char* filename, uint64_t* out_size)
{
    if (!bfs || !filename)
        return -1;

    struct bfs_file_entry entry;
    if (bfs_find(bfs, filename, &entry) != 0)
        return -1;

    if (out_size)
        *out_size = entry.size_bytes;

    return 0;
}

int bfs_read_file(struct bfs_instance* bfs, const char* filename, void* buffer, uint64_t max_size, uint64_t* out_size)
{
    if (!bfs || !filename || !buffer)
        return -1;

    struct bfs_file_entry entry;
    if (bfs_find(bfs, filename, &entry) != 0)
        return -1;

    uint64_t size = entry.size_bytes;
    if (out_size)
        *out_size = size;

    if (size == 0)
        return 0;

    if (max_size < size)
        return -2;

    uint64_t remaining = size;
    uint8_t* out = (uint8_t*)buffer;
    uint64_t page_num = entry.start_block;

    while (remaining > 0)
    {
        struct bfs_page* page = bfs_read_page(bfs, page_num);
        if (!page)
            return -3;

        uint64_t chunk = remaining > BFS_PAGE_SIZE ? BFS_PAGE_SIZE : remaining;
        memcpy(out, page, chunk);
        out += chunk;
        remaining -= chunk;
        page_num++;
    }

    return 0;
}

int bfs_write_file(struct bfs_instance* bfs, const char* filename, const void* buffer, uint64_t size)
{
    if (!bfs || !filename)
        return -1;

    uint64_t key = bfs_hash(filename);
    struct bfs_search_result search = bfs_search(bfs, key);

    uint64_t start_block = 0;
    uint64_t existing_size = 0;

    if (search.found)
    {
        start_block = (search.value >> 32) & 0xFFFFFFFF;
        existing_size = search.value & 0xFFFFFFFF;

        if (size > existing_size)
        {
            uint64_t blocks_needed = (size + BFS_PAGE_SIZE - 1) / BFS_PAGE_SIZE;
            if (blocks_needed > bfs->root->free_pages)
                return -2;

            start_block = blocks_needed ? bfs_alloc_pages(bfs, blocks_needed) : 0;
            if (blocks_needed && start_block == 0)
                return -2;
        }
    }
    else
    {
        uint64_t blocks_needed = (size + BFS_PAGE_SIZE - 1) / BFS_PAGE_SIZE;
        if (blocks_needed > bfs->root->free_pages)
            return -3;

        start_block = blocks_needed ? bfs_alloc_pages(bfs, blocks_needed) : 0;
        if (blocks_needed && start_block == 0)
            return -3;

        uint64_t value = (start_block << 32) | (size & 0xFFFFFFFF);
        if (bfs_insert(bfs, key, value) != 0)
            return -4;

        bfs->root->file_count++;
        bfs_flush_root(bfs);
    }

    if (search.found)
    {
        struct bfs_page* leaf = bfs_read_page(bfs, search.page_num);
        if (!leaf)
            return -5;

        leaf->values[search.index] = (start_block << 32) | (size & 0xFFFFFFFF);
        bfs_write_page(bfs, search.page_num, leaf);
        bfs_flush_root(bfs);
    }

    (void)bfs_name_table_upsert(bfs, key, filename, start_block, size);

    if (size == 0)
        return 0;

    uint64_t remaining = size;
    const uint8_t* in = (const uint8_t*)buffer;
    uint64_t page_num = start_block;

    while (remaining > 0)
    {
        uint8_t page_buf[BFS_PAGE_SIZE];
        uint64_t chunk = remaining > BFS_PAGE_SIZE ? BFS_PAGE_SIZE : remaining;

        memset(page_buf, 0, BFS_PAGE_SIZE);
        memcpy(page_buf, in, chunk);
        bfs_write_page(bfs, page_num, (struct bfs_page*)page_buf);

        in += chunk;
        remaining -= chunk;
        page_num++;
    }

    return 0;
}

// Delete a file (simplified - no rebalancing)
int bfs_delete(struct bfs_instance* bfs, const char* filename)
{
    uint64_t hash = bfs_hash(filename);
    struct bfs_search_result search = bfs_search(bfs, hash);
    
    if (!search.found)
        return -1;
    
    // Remove from leaf
    struct bfs_page* leaf = bfs_read_page(bfs, search.page_num);
    
    // Shift elements left
    for (int i = search.index; i < leaf->count - 1; i++)
    {
        leaf->keys[i] = leaf->keys[i+1];
        leaf->values[i] = leaf->values[i+1];
    }
    leaf->count--;
    bfs_write_page(bfs, search.page_num, leaf);
    
    bfs->root->file_count--;
    bfs_flush_root(bfs);

    bfs_name_table_remove(bfs, hash);
    
    return 0;
}

// List all files
int bfs_list(struct bfs_instance* bfs)
{
    tty_print("\nBranchFS File Listing:\n");
    tty_print("======================\n");
    
    if (bfs->root->file_count == 0)
    {
        tty_print("(empty)\n");
        return 0;
    }
    
    int have_names = 0;
    if (bfs->name_table_enabled && bfs_name_table_read(bfs, g_name_table_buf) == 0)
        have_names = 1;

    // Start from root and traverse to leftmost leaf
    uint64_t current_page_num = bfs->root->root_page;
    struct bfs_page* current = bfs_read_page(bfs, current_page_num);
    
    // Go to leftmost leaf
    while (current && current->level > 0)
    {
        current_page_num = current->values[0];
        current = bfs_read_page(bfs, current_page_num);
    }
    
    // Traverse leaf nodes
    int count = 0;
    while (current)
    {
        for (int i = 0; i < current->count; i++)
        {
            uint64_t start = (current->values[i] >> 32) & 0xFFFFFFFF;
            uint64_t size = current->values[i] & 0xFFFFFFFF;
            const char* name = 0;
            if (have_names)
            {
                for (int n = 0; n < (int)BFS_NAME_ENTRIES; n++)
                {
                    if (g_name_table_buf[n].hash == current->keys[i])
                    {
                        name = g_name_table_buf[n].name;
                        break;
                    }
                }
            }
            
            tty_print("File ");
            // Print count
            char buf[32];
            int idx = 0;
            int num = count + 1;
            if (num == 0) buf[idx++] = '0';
            else {
                int tmp = num, len = 0;
                while (tmp > 0) { tmp /= 10; len++; }
                tmp = num;
                for (int j = len - 1; j >= 0; j--) {
                    buf[j] = '0' + (tmp % 10);
                    tmp /= 10;
                }
                idx = len;
            }
            buf[idx] = '\0';
            tty_print(buf);
            
            if (name && name[0])
            {
                tty_print(": ");
                tty_print(name);
            }
            else
            {
                tty_print(": hash=0x");
                // Print hash in hex
                uint64_t h = current->keys[i];
                for (int j = 15; j >= 0; j--) {
                    int nibble = (h >> (j*4)) & 0xF;
                    tty_putchar(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
                }
            }
            
            tty_print(" block=");
            idx = 0;
            if (start == 0) buf[idx++] = '0';
            else {
                uint64_t tmp = start, len = 0;
                while (tmp > 0) { tmp /= 10; len++; }
                tmp = start;
                for (int j = len - 1; j >= 0; j--) {
                    buf[j] = '0' + (tmp % 10);
                    tmp /= 10;
                }
                idx = len;
            }
            buf[idx] = '\0';
            tty_print(buf);
            
            tty_print(" size=");
            idx = 0;
            if (size == 0) buf[idx++] = '0';
            else {
                uint64_t tmp = size, len = 0;
                while (tmp > 0) { tmp /= 10; len++; }
                tmp = size;
                for (int j = len - 1; j >= 0; j--) {
                    buf[j] = '0' + (tmp % 10);
                    tmp /= 10;
                }
                idx = len;
            }
            buf[idx] = '\0';
            tty_print(buf);
            tty_print(" bytes\n");
            
            count++;
        }
        
        // Move to next leaf
        if (current->next == 0)
            break;
        current = bfs_read_page(bfs, current->next);
    }
    
    return count;
}

/*
 * bfs_list_dir — list direct children of a directory.
 *
 * prefix : BFS path without leading slash ("" = root, "home" = /home).
 * out    : output buffer; entries are written as NUL-terminated strings
 *          back-to-back, list ends with an extra '\0'.
 *          Directories get a '/' appended to their name.
 * out_len: size of out in bytes.
 * Returns number of entries found, or -1 on error.
 */
int bfs_list_dir(struct bfs_instance* bfs, const char* prefix, char* out, int out_len)
{
    if (!bfs || !out || out_len <= 1)
        return -1;

    if (!bfs->name_table_enabled ||
        bfs_name_table_read(bfs, g_name_table_buf) != 0)
    {
        out[0] = '\0';
        return 0;
    }

    int prefix_len = 0;
    while (prefix[prefix_len]) prefix_len++;

    int count = 0;
    int pos   = 0;

    for (int i = 0; i < (int)BFS_NAME_ENTRIES; i++)
    {
        if (g_name_table_buf[i].hash == 0)
            continue;

        const char* name   = g_name_table_buf[i].name;
        int         is_dir = (g_name_table_buf[i].size_bytes == 0);
        const char* entry  = NULL;  /* points to just the leaf component */

        if (prefix_len == 0)
        {
            /* Root: include if name contains no '/' */
            int has_slash = 0;
            for (int j = 0; name[j]; j++)
                if (name[j] == '/') { has_slash = 1; break; }
            if (!has_slash)
                entry = name;
        }
        else
        {
            /* Subdirectory: name must start with "prefix/" with no more '/' after */
            int match = 1;
            for (int j = 0; j < prefix_len; j++)
                if (name[j] != prefix[j]) { match = 0; break; }

            if (match && name[prefix_len] == '/')
            {
                const char* rest = name + prefix_len + 1;
                int has_slash = 0;
                for (int j = 0; rest[j]; j++)
                    if (rest[j] == '/') { has_slash = 1; break; }
                if (!has_slash && rest[0] != '\0')
                    entry = rest;
            }
        }

        if (!entry)
            continue;

        int elen = 0;
        while (entry[elen]) elen++;
        int need = elen + (is_dir ? 1 : 0) + 1; /* name [/] \0 */
        if (pos + need + 1 >= out_len)           /* +1 for terminating \0 */
            break;

        for (int j = 0; j < elen; j++)
            out[pos++] = entry[j];
        if (is_dir)
            out[pos++] = '/';
        out[pos++] = '\0';
        count++;
    }

    out[pos] = '\0'; /* double-NUL: end of list */
    return count;
}

// Print tree structure (debug — output goes to serial)
void bfs_print_tree(struct bfs_instance* bfs)
{
    serial_write("[BFS] root_page=");
    serial_hex(bfs->root->root_page);
    serial_write(" total_pages=");
    serial_hex(bfs->root->total_pages);
    serial_write(" free_pages=");
    serial_hex(bfs->root->free_pages);
    serial_write(" files=");
    serial_hex(bfs->root->file_count);
    serial_write("\n");
}
