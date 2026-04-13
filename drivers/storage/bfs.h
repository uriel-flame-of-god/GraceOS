// ============================
// BranchFS-BT (Balanced Tree Filesystem)
// Phase 1: Minimal Tree Implementation
// ============================

#ifndef BFS_H
#define BFS_H

#include "../../lib/libc/int.h"

// Page size (4KB standard)
#define BFS_PAGE_SIZE 4096
#define BFS_MAGIC 0x42465301  // "BFS\x01"
#define BFS_VERSION 2

// Name table (page 2) for simple filename listings
#define BFS_NAME_TABLE_PAGE 2
#define BFS_NAME_MAX 48

// B+ tree configuration
#define BFS_MAX_KEYS 127      // Keys per node
#define BFS_MAX_VALUES 127    // Values per node (leaf) or children (internal)
#define BFS_MIN_KEYS (BFS_MAX_KEYS / 2)

// File extent (start block + size)
struct bfs_extent {
    uint64_t start_block;
    uint64_t block_count;
};

// B+ Tree Page (4KB)
struct bfs_page {
    uint32_t magic;           // Magic number
    uint16_t level;           // 0 = leaf, >0 = internal
    uint16_t count;           // Number of keys in this node
    uint64_t next;            // Next leaf page (for leaf nodes, 0 for internal)
    uint64_t parent;          // Parent page number
    
    // Key-value pairs
    uint64_t keys[BFS_MAX_KEYS];        // Filename hashes or inode numbers
    uint64_t values[BFS_MAX_VALUES];    // For leaf: extent info, for internal: child page numbers
} __attribute__((aligned(8)));

// Filename table entry (stored on page 2)
struct bfs_name_entry {
    uint64_t hash;
    uint64_t start_block;
    uint64_t size_bytes;
    char name[BFS_NAME_MAX];
} __attribute__((aligned(8)));

// Root node descriptor
struct bfs_root {
    uint32_t magic;
    uint32_t version;
    uint64_t root_page;       // Root page number
    uint64_t total_pages;     // Total pages in filesystem
    uint64_t free_pages;      // Free pages
    uint64_t next_free;       // Next free page number
    uint64_t file_count;      // Number of files
} __attribute__((aligned(8)));

// File entry (stored as value in leaf nodes)
struct bfs_file_entry {
    uint64_t inode;
    uint64_t start_block;
    uint64_t size_bytes;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((aligned(8)));

// Search result
struct bfs_search_result {
    int found;
    uint64_t page_num;
    int index;
    uint64_t value;
};

// BranchFS backend type
typedef enum {
    BFS_BACKEND_PMEM = 1,
    BFS_BACKEND_RAM  = 2
} bfs_backend_t;

// BranchFS Instance
struct bfs_instance {
    struct bfs_root* root;
    uint64_t disk_size;       // Total size in bytes
    uint8_t* ram_base;        // RAM base for memory-backed mode

    uint8_t root_page[BFS_PAGE_SIZE];
    uint8_t page_buf[BFS_PAGE_SIZE];
    uint64_t page_buf_num;
    bfs_backend_t backend;
    int name_table_enabled;
};

// ========== Core Functions ==========

// Initialize filesystem (ram_base == 0 uses persistent memory backend)
int bfs_init(struct bfs_instance* bfs, uint64_t disk_size, void* ram_base);

// Format disk with BranchFS
int bfs_format(struct bfs_instance* bfs);

// Create a file
int bfs_create(struct bfs_instance* bfs, const char* filename, uint64_t size);

// Find a file
int bfs_find(struct bfs_instance* bfs, const char* filename, struct bfs_file_entry* entry);

// Delete a file
int bfs_delete(struct bfs_instance* bfs, const char* filename);

// List all files
int bfs_list(struct bfs_instance* bfs);

// List direct children of a directory (prefix = BFS path, no leading slash).
// Entries are written NUL-terminated back-to-back; list ends with extra NUL.
// Directories get a '/' suffix. Returns entry count or -1 on error.
int bfs_list_dir(struct bfs_instance* bfs, const char* prefix, char* out, int out_len);

// ========== Helper Functions ==========

// Hash filename
uint64_t bfs_hash(const char* str);

// Read page from disk
struct bfs_page* bfs_read_page(struct bfs_instance* bfs, uint64_t page_num);

// Write page to disk
void bfs_write_page(struct bfs_instance* bfs, uint64_t page_num, struct bfs_page* page);

// Allocate a new page
uint64_t bfs_alloc_page(struct bfs_instance* bfs);

// Allocate contiguous pages
uint64_t bfs_alloc_pages(struct bfs_instance* bfs, uint64_t pages);

// Free a page
void bfs_free_page(struct bfs_instance* bfs, uint64_t page_num);

// Search tree for key
struct bfs_search_result bfs_search(struct bfs_instance* bfs, uint64_t key);

// Insert key-value pair
int bfs_insert(struct bfs_instance* bfs, uint64_t key, uint64_t value);

// Read file contents
int bfs_read_file(struct bfs_instance* bfs, const char* filename, void* buffer, uint64_t max_size, uint64_t* out_size);

// Write file contents
int bfs_write_file(struct bfs_instance* bfs, const char* filename, const void* buffer, uint64_t size);

// Get file size
int bfs_file_size(struct bfs_instance* bfs, const char* filename, uint64_t* out_size);

// Print tree structure (debug)
void bfs_print_tree(struct bfs_instance* bfs);

#endif // BFS_H
