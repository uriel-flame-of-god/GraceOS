# BranchFS Filesystem Documentation

## Overview
BranchFS (BFS) is a B+ tree-based filesystem designed for GraceOS. It provides efficient key-value storage with ordered traversal capabilities, optimized for page-sized access on persistent memory.

**Status**: Phase 2 - Persistent memory backend (IDE/NVMe) with RAM fallback

## Design Philosophy

### Goals
1. **Simplicity**: Minimal implementation for educational purposes
2. **Efficiency**: O(log n) search, insert, delete operations
3. **Ordered Access**: Support for range queries and iteration
4. **Extensibility**: Foundation for future enhancements

### Non-Goals (Current Phase)
- Journaling or crash recovery
- Access control or permissions
- Compression or encryption

## Architecture

### Storage Model
```
┌─────────────────────────────────────────────┐
│       Persistent Memory (IDE/NVMe)          │
├─────────────────────────────────────────────┤
│ Page 0: Root Descriptor (4KB)               │
│   - Magic number (0x42465301)               │
│   - Version (1)                             │
│   - Root page pointer                       │
│   - Total pages / Free pages                │
│   - File count                              │
├─────────────────────────────────────────────┤
│ Page 1: B+ Tree Root Node (4KB)             │
├─────────────────────────────────────────────┤
│ Page 2: Data Page (4KB)                     │
├─────────────────────────────────────────────┤
│ Page 3: Data Page (4KB)                     │
├─────────────────────────────────────────────┤

### Persistent Memory Backend
BranchFS stores pages on a persistent memory device selected at boot:

- **NVMe**: preferred when a controller is present
- **IDE (ATA PIO)**: fallback for older/virtualized targets
- **RAM fallback**: used only when no persistent backend is detected

BranchFS consumes the first region of the persistent device. The remaining space is reserved for swap.
│ ...                                         │
└─────────────────────────────────────────────┘
```

### Page Size
**4096 bytes (4KB)**

Chosen because:
- Standard x86 page size
- Good balance between space and overhead
- Fits nicely in L1/L2 cache
- Aligns with future disk I/O

### B+ Tree Structure

#### Internal Nodes
```
┌────────────────────────────────────────┐
│ magic (4B) | level (2B) | count (2B)  │
│ next (8B) | parent (8B)               │
├────────────────────────────────────────┤
│ keys[0]    keys[1]    keys[2]   ...   │  (uint64_t array)
│ child[0]   child[1]   child[2]  ...   │  (page numbers)
└────────────────────────────────────────┘

Properties:
- level > 0 (height in tree)
- count = number of keys
- children = count + 1
- keys[i] = minimum key in subtree child[i+1]
```

#### Leaf Nodes
```
┌────────────────────────────────────────┐
│ magic (4B) | level (2B) | count (2B)  │
│ next (8B) | parent (8B)               │
├────────────────────────────────────────┤
│ keys[0]    keys[1]    keys[2]   ...   │  (uint64_t array)
│ values[0]  values[1]  values[2] ...   │  (extent info)
└────────────────────────────────────────┘

Properties:
- level = 0 (leaf)
- count = number of key-value pairs
- next = pointer to next leaf (for iteration)
- values = file extent information
```

### Capacity Calculation
```c
// Page layout:
// - Header: 24 bytes (magic, level, count, next, parent)
// - Usable: 4096 - 24 = 4072 bytes

// Each entry is 16 bytes (8B key + 8B value)
// Max entries: 4072 / 16 = 254 entries per node

#define BFS_PAGE_SIZE 4096
#define BFS_MAX_KEYS 254
#define BFS_MAX_VALUES 254
```

## Data Structures

### Root Descriptor
```c
struct bfs_root {
    uint32_t magic;           // 0x42465301 ("BFS\x01")
    uint32_t version;         // Filesystem version (1)
    uint64_t root_page;       // Root B+ tree node page number
    uint64_t total_pages;     // Total pages in filesystem
    uint64_t free_pages;      // Number of free pages
    uint64_t next_free;       // Next allocatable page
    uint64_t file_count;      // Number of files
} __attribute__((aligned(8)));
```

### B+ Tree Page
```c
struct bfs_page {
    uint32_t magic;                  // BFS_MAGIC
    uint16_t level;                  // 0 = leaf, >0 = internal
    uint16_t count;                  // Number of keys
    uint64_t next;                   // Next leaf (0 for internal)
    uint64_t parent;                 // Parent page number
    
    uint64_t keys[BFS_MAX_KEYS];     // Sorted keys
    uint64_t values[BFS_MAX_VALUES]; // Values or child pages
} __attribute__((aligned(8)));
```

### File Entry
```c
struct bfs_file_entry {
    uint64_t inode;          // File inode number
    uint64_t start_block;    // Starting page number
    uint64_t size_bytes;     // File size in bytes
    uint32_t flags;          // File flags
    uint32_t reserved;       // Reserved for future use
} __attribute__((aligned(8)));
```

### Search Result
```c
struct bfs_search_result {
    int found;               // 1 if key found, 0 otherwise
    uint64_t page_num;       // Page containing key (or where it should be)
    int index;               // Index in page (-1 if not found)
    uint64_t value;          // Value associated with key
};
```

### Instance
```c
struct bfs_instance {
    struct bfs_root* root;   // Pointer to root descriptor
    uint64_t disk_size;      // Size of filesystem region in bytes
    uint8_t* ram_base;       // RAM base for fallback backend
    uint8_t root_page[BFS_PAGE_SIZE];
    uint8_t page_buf[BFS_PAGE_SIZE];
    uint64_t page_buf_num;
    int backend;             // PMEM or RAM
};
```

## Core Operations

### Initialization

#### Format New Filesystem
```c
int bfs_format(struct bfs_instance* bfs)
{
    // 1. Clear root page
    memset(bfs->root_page, 0, BFS_PAGE_SIZE);
    
    // 2. Initialize root descriptor
    bfs->root->magic = BFS_MAGIC;
    bfs->root->version = 1;
    bfs->root->total_pages = bfs->disk_size / BFS_PAGE_SIZE;
    bfs->root->free_pages = bfs->root->total_pages - 2;
    bfs->root->next_free = 2;
    bfs->root->file_count = 0;
    bfs->root->root_page = 1;
    bfs_write_page(bfs, 0, (struct bfs_page*)bfs->root_page);
    
    // 3. Create empty root node
    struct bfs_page* root_node = bfs_read_page(bfs, 1);
    memset(root_node, 0, BFS_PAGE_SIZE);
    root_node->magic = BFS_MAGIC;
    root_node->level = 0;      // Start as leaf
    root_node->count = 0;
    root_node->next = 0;
    root_node->parent = 0;
    bfs_write_page(bfs, 1, root_node);
    
    return 0;
}
```

#### Mount Existing Filesystem
```c
int bfs_init(struct bfs_instance* bfs, uint64_t disk_size, void* ram_base)
{
    bfs->disk_size = disk_size;
    bfs->ram_base = (uint8_t*)ram_base;
    bfs->backend = ram_base ? BFS_BACKEND_RAM : BFS_BACKEND_PMEM;
    bfs->root = (struct bfs_root*)bfs->root_page;
    bfs_read_page(bfs, 0);  // populate root_page
    
    // Check if formatted
    if (bfs->root->magic == BFS_MAGIC) {
        return 0;  // Already initialized
    }
    
    return -1;  // Need to format
}
```

### Search Operation

**Algorithm**: Binary search through tree levels

```c
struct bfs_search_result bfs_search(struct bfs_instance* bfs, uint64_t key)
{
    struct bfs_search_result result = {0};
    
    // 1. Start at root
    uint64_t current_page_num = bfs->root->root_page;
    struct bfs_page* current = bfs_read_page(bfs, current_page_num);
    
    // 2. Traverse tree
    while (current->level > 0) {
        // Internal node - find child to follow
        int i;
        for (i = 0; i < current->count; i++) {
            if (key < current->keys[i]) {
                break;
            }
        }
        
        // Navigate to child
        current_page_num = current->values[i];
        current = bfs_read_page(bfs, current_page_num);
    }
    
    // 3. Search in leaf
    for (int i = 0; i < current->count; i++) {
        if (current->keys[i] == key) {
            result.found = 1;
            result.page_num = current_page_num;
            result.index = i;
            result.value = current->values[i];
            return result;
        }
    }
    
    // 4. Not found
    result.page_num = current_page_num;
    return result;
}
```

**Complexity**: O(log n) where n = number of entries

### Insertion Operation

**Algorithm**: Search, then insert with potential split

```c
int bfs_insert(struct bfs_instance* bfs, uint64_t key, uint64_t value)
{
    // 1. Search for insertion point
    struct bfs_search_result search = bfs_search(bfs, key);
    
    if (search.found) {
        return -1;  // Key already exists
    }
    
    // 2. Get leaf page
    struct bfs_page* leaf = bfs_read_page(bfs, search.page_num);
    
    // 3. Check if split needed
    if (leaf->count >= BFS_MAX_KEYS) {
        // Split leaf node
        uint64_t new_page_num = bfs_alloc_page(bfs);
        struct bfs_page* new_leaf = bfs_read_page(bfs, new_page_num);
        
        // Move half the entries to new page
        int mid = leaf->count / 2;
        new_leaf->count = leaf->count - mid;
        leaf->count = mid;
        
        memcpy(new_leaf->keys, &leaf->keys[mid], 
               new_leaf->count * sizeof(uint64_t));
        memcpy(new_leaf->values, &leaf->values[mid],
               new_leaf->count * sizeof(uint64_t));
        
        // Update links
        new_leaf->next = leaf->next;
        leaf->next = new_page_num;
        
        // Decide which leaf gets new key
        if (key >= new_leaf->keys[0]) {
            leaf = new_leaf;
        }
    }
    
    // 4. Find insertion position
    int i;
    for (i = 0; i < leaf->count; i++) {
        if (key < leaf->keys[i]) {
            break;
        }
    }
    
    // 5. Shift entries and insert
    memmove(&leaf->keys[i+1], &leaf->keys[i],
            (leaf->count - i) * sizeof(uint64_t));
    memmove(&leaf->values[i+1], &leaf->values[i],
            (leaf->count - i) * sizeof(uint64_t));
    
    leaf->keys[i] = key;
    leaf->values[i] = value;
    leaf->count++;
    
    return 0;
}
```

**Complexity**: O(log n) + O(m) for shift where m = entries per page

### Key Hashing

**Function**: DJB2 hash algorithm

```c
uint64_t bfs_hash(const char* str)
{
    uint64_t hash = 5381;
    int c;
    
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;  // hash * 33 + c
    }
    
    return hash;
}
```

**Usage**:
```c
uint64_t key = bfs_hash("myfile.txt");
bfs_insert(bfs, key, file_entry);
```

## File Operations

### Create File
```c
int bfs_create_file(struct bfs_instance* bfs, const char* name, 
                    uint64_t size)
{
    // 1. Hash filename
    uint64_t key = bfs_hash(name);
    
    // 2. Allocate pages for file data
    uint64_t pages_needed = (size + BFS_PAGE_SIZE - 1) / BFS_PAGE_SIZE;
    uint64_t start_page = bfs_alloc_pages(bfs, pages_needed);
    
    if (start_page == 0) {
        return -1;  // No space
    }
    
    // 3. Create file entry
    struct bfs_file_entry entry;
    entry.inode = bfs->root->file_count++;
    entry.start_block = start_page;
    entry.size_bytes = size;
    entry.flags = 0;
    
    // 4. Insert into B+ tree
    return bfs_insert(bfs, key, (uint64_t)&entry);
}
```

### Read File
```c
int bfs_read_file(struct bfs_instance* bfs, const char* name,
                  void* buffer, uint64_t size)
{
    // 1. Find file entry
    uint64_t key = bfs_hash(name);
    struct bfs_search_result result = bfs_search(bfs, key);
    
    if (!result.found) {
        return -1;  // File not found
    }
    
    // 2. Get file entry
    struct bfs_file_entry* entry = (struct bfs_file_entry*)result.value;
    
    // 3. Check size
    if (size > entry->size_bytes) {
        size = entry->size_bytes;
    }
    
    // 4. Copy data
    void* file_data = bfs_read_page(bfs, entry->start_block);
    memcpy(buffer, file_data, size);
    
    return size;
}
```

### List Files
```c
void bfs_list_files(struct bfs_instance* bfs)
{
    // Start at leftmost leaf
    uint64_t page_num = find_leftmost_leaf(bfs);
    
    while (page_num != 0) {
        struct bfs_page* leaf = bfs_read_page(bfs, page_num);
        
        // Print all entries in leaf
        for (int i = 0; i < leaf->count; i++) {
            struct bfs_file_entry* entry = 
                (struct bfs_file_entry*)leaf->values[i];
            
            tty_print("File: ");
            tty_print_hex(entry->inode);
            tty_print(" Size: ");
            tty_print_hex(entry->size_bytes);
            tty_print("\n");
        }
        
        // Move to next leaf
        page_num = leaf->next;
    }
}
```

## Directory Support

BranchFS does not have a separate directory inode type. Directories are represented as **zero-size BFS entries** whose names encode the full path using `/` as a separator.

### Path Format Convention

BFS stores all entries (files and directories) using paths **without a leading slash**:

| BFS key name | Represents |
|---|---|
| `""` (empty string) | filesystem root |
| `home` | directory `/home` |
| `home/user` | directory `/home/user` |
| `home/user/notes.txt` | file `/home/user/notes.txt` |

The shell maintains `shell_cwd` as a `/`-prefixed absolute path (e.g. `/home`) and strips the leading slash when forming BFS key names.

### Directory Entries

A directory is created with `bfs_create` (or equivalent) passing `size = 0`. There is no flags field to distinguish a directory from an empty file; the convention is that any zero-size entry whose name does not conflict with a real file is treated as a directory placeholder. The shell's `mkdir` command relies on this convention.

### mkdir

The shell command `mkdir <name>` creates a directory at the current path:

- Accepts absolute or relative paths and resolves `.` / `..` segments.

```
GraceOS /home> mkdir docs
```

Internally this resolves to the BFS path `home/docs` and calls `bfs_create` with `size = 0`. If the entry already exists the command reports an error.

### cd / chdir

`cd <path>` and `chdir <path>` navigate the shell's current directory:

- Accepts absolute paths (`/home/user`) and relative paths (`docs`, `..`, `.`)
- Resolves `.` (no-op) and `..` (strips last path component)
- With no arguments, `cd` resets the current directory to `/`
- Verifies the resolved BFS path exists as a zero-size entry before updating `shell_cwd`
- Reports an error if the target does not exist

### bfs_list_dir

```c
int bfs_list_dir(struct bfs_instance* bfs,
                 const char* prefix,
                 char* out,
                 int out_len);
```

Lists the direct children of a directory.

**Parameters**

| Parameter | Description |
|---|---|
| `bfs` | BFS instance |
| `prefix` | BFS path of the directory to list, without leading slash. Pass `""` for the root. |
| `out` | Output buffer. Children are written as NUL-terminated strings packed back-to-back, terminated by a double-NUL. |
| `out_len` | Size of the output buffer in bytes. |

**Returns**: number of entries written, or 0 if the directory is empty or not found.

**Behaviour**

- For the root (`prefix = ""`): includes all entries whose name contains no `/`.
- For a subdirectory (`prefix = "home"`): includes entries whose name starts with `"home/"` and contains no further `/` after the prefix.
- Directory entries (zero-size) are written with a trailing `/` appended to the name in the output buffer.
- The shell's `ls` command uses this function and colours directory names blue and file names grey.
- The shell's `ls` prints `.` always and `..` when not at root.

**Example**

```c
char buf[4096];
int count = bfs_list_dir(&bfs, "home", buf, sizeof(buf));

// buf contains: "user/\0notes.txt\0\0"
// count = 2
```

## Device-Backed I/O

### Page Reading
```c
struct bfs_page* bfs_read_page(struct bfs_instance* bfs, uint64_t page_num)
{
    // 1. Bounds check
    if (page_num >= bfs->root->total_pages) {
        return NULL;
    }
    
    // 2. Calculate offset
    uint64_t offset = page_num * BFS_PAGE_SIZE;
    
    // 3. Read into internal page buffer
    bfs_device_read(bfs, offset, bfs->page_buf, BFS_PAGE_SIZE);
    return (struct bfs_page*)bfs->page_buf;
}
```

### Page Writing
```c
void bfs_write_page(struct bfs_instance* bfs, uint64_t page_num,
                    struct bfs_page* page)
{
    uint64_t offset = page_num * BFS_PAGE_SIZE;
    bfs_device_write(bfs, offset, page, BFS_PAGE_SIZE);
}
```

## Page Allocation

### Allocate Single Page
```c
uint64_t bfs_alloc_page(struct bfs_instance* bfs)
{
    if (bfs->root->free_pages == 0) {
        return 0;  // Out of space
    }
    
    uint64_t page_num = bfs->root->next_free;
    bfs->root->next_free++;
    bfs->root->free_pages--;
    
    // Zero out page
    struct bfs_page* page = bfs_read_page(bfs, page_num);
    memset(page, 0, BFS_PAGE_SIZE);
    page->magic = BFS_MAGIC;
    bfs_write_page(bfs, page_num, page);
    
    return page_num;
}
```

### Free Page
```c
void bfs_free_page(struct bfs_instance* bfs, uint64_t page_num)
{
    // Simple implementation: just increment free count
    // No free list yet
    bfs->root->free_pages++;
    
    // Future: Add to free list for reuse
}
```

## Debugging

### Print Tree Structure
```c
void bfs_print_tree(struct bfs_instance* bfs)
{
    tty_print("BFS Tree Structure:\n");
    print_node_recursive(bfs, bfs->root->root_page, 0);
}

void print_node_recursive(struct bfs_instance* bfs, uint64_t page_num, int depth)
{
    struct bfs_page* node = bfs_read_page(bfs, page_num);
    
    // Indent
    for (int i = 0; i < depth; i++) {
        tty_print("  ");
    }
    
    // Print node info
    tty_print("Page ");
    tty_print_hex(page_num);
    tty_print(" Level ");
    tty_print_hex(node->level);
    tty_print(" Count ");
    tty_print_hex(node->count);
    tty_print("\n");
    
    // Print keys
    for (int i = 0; i < node->count; i++) {
        for (int j = 0; j < depth + 1; j++) {
            tty_print("  ");
        }
        tty_print("Key[");
        tty_print_hex(i);
        tty_print("] = ");
        tty_print_hex(node->keys[i]);
        tty_print("\n");
    }
    
    // Recurse for internal nodes
    if (node->level > 0) {
        for (int i = 0; i <= node->count; i++) {
            print_node_recursive(bfs, node->values[i], depth + 1);
        }
    }
}
```

### Validate Tree
```c
int bfs_validate(struct bfs_instance* bfs)
{
    return validate_node(bfs, bfs->root->root_page, 0, UINT64_MAX);
}

int validate_node(struct bfs_instance* bfs, uint64_t page_num,
                  uint64_t min_key, uint64_t max_key)
{
    struct bfs_page* node = bfs_read_page(bfs, page_num);
    
    // Check magic
    if (node->magic != BFS_MAGIC) {
        tty_print("ERROR: Bad magic\n");
        return 0;
    }
    
    // Check key ordering
    for (int i = 1; i < node->count; i++) {
        if (node->keys[i] <= node->keys[i-1]) {
            tty_print("ERROR: Keys not sorted\n");
            return 0;
        }
    }
    
    // Check key bounds
    if (node->count > 0) {
        if (node->keys[0] < min_key || node->keys[node->count-1] > max_key) {
            tty_print("ERROR: Keys out of bounds\n");
            return 0;
        }
    }
    
    // Recurse
    if (node->level > 0) {
        for (int i = 0; i <= node->count; i++) {
            uint64_t child_min = (i == 0) ? min_key : node->keys[i-1];
            uint64_t child_max = (i == node->count) ? max_key : node->keys[i];
            
            if (!validate_node(bfs, node->values[i], child_min, child_max)) {
                return 0;
            }
        }
    }
    
    return 1;
}
```

## Future Enhancements

### Phase 2: Disk Persistence
- Block device driver (IDE/AHCI)
- Write-back caching
- Journal for crash recovery
- Superblock with filesystem metadata

### Phase 3: Advanced Features
- File permissions
- Timestamps (create, modify, access)
- Hard links and symbolic links
- Extended attributes

### Phase 4: Performance
- Extent trees for large files
- Delayed allocation
- Multi-block I/O
- Buffer cache
- Prefetching

### Phase 5: Reliability
- Checksums for data integrity
- RAID support
- Snapshots
- Online fsck
