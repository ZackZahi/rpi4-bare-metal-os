// fs.h - In-memory filesystem (ramfs)
//
// Simple tree-structured filesystem stored entirely in RAM.
// Supports directories and files with read/write content.
//
// Path format: /dir/subdir/file (absolute paths, '/' as root)

#ifndef FS_H
#define FS_H

#define FS_NAME_MAX     32      // Max filename length
#define FS_PATH_MAX     128     // Max path length
#define FS_MAX_NODES    64      // Max total files + directories
#define FS_MAX_DATA     4096    // Max file content size

typedef enum {
    FS_FILE,
    FS_DIR
} fs_node_type_t;

typedef struct fs_node {
    char name[FS_NAME_MAX];
    fs_node_type_t type;
    struct fs_node *parent;
    // For directories: linked list of children
    struct fs_node *children;
    struct fs_node *next_sibling;
    // For files: content
    char *data;
    unsigned long size;
} fs_node_t;

// Initialize filesystem with root directory
void fs_init(void);

// Navigation
fs_node_t *fs_get_root(void);
fs_node_t *fs_get_cwd(void);
void fs_set_cwd(fs_node_t *dir);

// Path resolution: returns node at path, or 0 if not found
// Supports absolute (/foo/bar) and relative (foo/bar) paths
fs_node_t *fs_resolve(const char *path);

// Directory operations
fs_node_t *fs_mkdir(const char *path);          // Create directory
int fs_rmdir(const char *path);                  // Remove empty directory

// File operations
fs_node_t *fs_touch(const char *path);           // Create empty file
fs_node_t *fs_write(const char *path, const char *content);  // Write content
const char *fs_read(const char *path, unsigned long *size);   // Read content
int fs_rm(const char *path);                     // Remove file

// Listing
void fs_ls(const char *path);                    // List directory contents

// Build full path string for a node
void fs_get_path(fs_node_t *node, char *buf, int bufsize);

#endif // FS_H
