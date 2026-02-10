// fs.c - In-memory filesystem (ramfs)
//
// Tree structure: each directory has a linked list of children.
// File content is stored via kmalloc'd buffers.
// Nodes are allocated from a static pool (no fragmentation).

#include "fs.h"
#include "uart.h"
#include "memory.h"

// ---- Node pool ----

static fs_node_t node_pool[FS_MAX_NODES];
static int nodes_used = 0;

static fs_node_t *root = 0;
static fs_node_t *cwd = 0;

// ---- String helpers ----

static int fs_strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) { a++; b++; }
    return *(unsigned char *)a - *(unsigned char *)b;
}

static int fs_strlen(const char *s) {
    int n = 0;
    while (*s++) n++;
    return n;
}

static void fs_strcpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static void fs_strncpy(char *dst, const char *src, int n) {
    while (n > 0 && *src) { *dst++ = *src++; n--; }
    *dst = '\0';
}

// ---- Allocate a new node ----

static fs_node_t *alloc_node(const char *name, fs_node_type_t type) {
    if (nodes_used >= FS_MAX_NODES) {
        uart_puts("[fs] ERROR: node pool full\n");
        return 0;
    }

    fs_node_t *node = &node_pool[nodes_used++];
    fs_strncpy(node->name, name, FS_NAME_MAX - 1);
    node->type = type;
    node->parent = 0;
    node->children = 0;
    node->next_sibling = 0;
    node->data = 0;
    node->size = 0;

    return node;
}

// Free a node (mark slot reusable — simple version just leaks)
// For a bare-metal OS with 64 slots this is fine
static void free_node(fs_node_t *node) {
    if (node->data) {
        kfree(node->data);
        node->data = 0;
    }
    node->size = 0;
    node->name[0] = '\0';
    // Note: we don't reclaim pool slots — kept simple
}

// ---- Add child to directory ----

static void add_child(fs_node_t *dir, fs_node_t *child) {
    child->parent = dir;
    child->next_sibling = dir->children;
    dir->children = child;
}

// ---- Remove child from directory ----

static void remove_child(fs_node_t *dir, fs_node_t *child) {
    fs_node_t **pp = &dir->children;
    while (*pp) {
        if (*pp == child) {
            *pp = child->next_sibling;
            child->next_sibling = 0;
            child->parent = 0;
            return;
        }
        pp = &(*pp)->next_sibling;
    }
}

// ---- Find child by name in a directory ----

static fs_node_t *find_child(fs_node_t *dir, const char *name) {
    if (!dir || dir->type != FS_DIR) return 0;
    fs_node_t *child = dir->children;
    while (child) {
        if (fs_strcmp(child->name, name) == 0)
            return child;
        child = child->next_sibling;
    }
    return 0;
}

// ---- Path resolution ----

// Parse next component from path. Returns length, advances *path.
static int next_component(const char **path, char *comp, int maxlen) {
    while (**path == '/') (*path)++;  // Skip leading slashes
    if (**path == '\0') return 0;

    int len = 0;
    while (**path && **path != '/' && len < maxlen - 1) {
        comp[len++] = *(*path)++;
    }
    comp[len] = '\0';
    return len;
}

// Resolve parent directory of a path, and return the final component name.
// E.g. "/foo/bar/baz" → resolves to /foo/bar, basename = "baz"
static fs_node_t *resolve_parent(const char *path, char *basename) {
    // Determine starting point
    fs_node_t *cur;
    if (path[0] == '/') {
        cur = root;
        path++;
    } else {
        cur = cwd;
    }

    // Walk all components except the last one
    char comp[FS_NAME_MAX];
    const char *remaining = path;

    // Find the last '/' to split parent/basename
    const char *last_slash = 0;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }

    if (!last_slash) {
        // No slash — basename is the whole thing, parent is cur
        fs_strncpy(basename, path, FS_NAME_MAX - 1);
        return cur;
    }

    // Walk to the parent directory
    while (remaining < last_slash) {
        int len = next_component(&remaining, comp, FS_NAME_MAX);
        if (len == 0) break;

        if (fs_strcmp(comp, ".") == 0) continue;
        if (fs_strcmp(comp, "..") == 0) {
            if (cur->parent) cur = cur->parent;
            continue;
        }

        fs_node_t *child = find_child(cur, comp);
        if (!child || child->type != FS_DIR) return 0;
        cur = child;
    }

    // Extract basename (after last slash)
    last_slash++;
    fs_strncpy(basename, last_slash, FS_NAME_MAX - 1);
    return cur;
}

// ---- Public API ----

void fs_init(void) {
    for (int i = 0; i < FS_MAX_NODES; i++) {
        node_pool[i].name[0] = '\0';
        node_pool[i].type = FS_DIR;
        node_pool[i].parent = 0;
        node_pool[i].children = 0;
        node_pool[i].next_sibling = 0;
        node_pool[i].data = 0;
        node_pool[i].size = 0;
    }
    nodes_used = 0;

    root = alloc_node("/", FS_DIR);
    root->parent = root;  // Root's parent is itself
    cwd = root;
}

fs_node_t *fs_get_root(void) { return root; }
fs_node_t *fs_get_cwd(void)  { return cwd; }

void fs_set_cwd(fs_node_t *dir) {
    if (dir && dir->type == FS_DIR)
        cwd = dir;
}

fs_node_t *fs_resolve(const char *path) {
    if (!path || !path[0]) return cwd;

    fs_node_t *cur;
    if (path[0] == '/') {
        cur = root;
        path++;
    } else {
        cur = cwd;
    }

    char comp[FS_NAME_MAX];
    while (next_component(&path, comp, FS_NAME_MAX) > 0) {
        if (fs_strcmp(comp, ".") == 0) continue;
        if (fs_strcmp(comp, "..") == 0) {
            if (cur->parent) cur = cur->parent;
            continue;
        }
        fs_node_t *child = find_child(cur, comp);
        if (!child) return 0;
        cur = child;
    }

    return cur;
}

fs_node_t *fs_mkdir(const char *path) {
    char basename[FS_NAME_MAX];
    fs_node_t *parent = resolve_parent(path, basename);

    if (!parent || parent->type != FS_DIR) {
        uart_puts("mkdir: parent directory not found\n");
        return 0;
    }

    if (basename[0] == '\0') {
        uart_puts("mkdir: missing directory name\n");
        return 0;
    }

    if (find_child(parent, basename)) {
        uart_puts("mkdir: '");
        uart_puts(basename);
        uart_puts("' already exists\n");
        return 0;
    }

    fs_node_t *dir = alloc_node(basename, FS_DIR);
    if (!dir) return 0;
    add_child(parent, dir);
    return dir;
}

int fs_rmdir(const char *path) {
    fs_node_t *node = fs_resolve(path);
    if (!node) {
        uart_puts("rmdir: not found\n");
        return -1;
    }
    if (node->type != FS_DIR) {
        uart_puts("rmdir: not a directory\n");
        return -1;
    }
    if (node == root) {
        uart_puts("rmdir: cannot remove root\n");
        return -1;
    }
    if (node->children) {
        uart_puts("rmdir: directory not empty\n");
        return -1;
    }
    if (node == cwd) {
        // Move cwd to parent before removing
        cwd = node->parent;
    }
    remove_child(node->parent, node);
    free_node(node);
    return 0;
}

fs_node_t *fs_touch(const char *path) {
    // If file already exists, just return it
    fs_node_t *existing = fs_resolve(path);
    if (existing) return existing;

    char basename[FS_NAME_MAX];
    fs_node_t *parent = resolve_parent(path, basename);

    if (!parent || parent->type != FS_DIR) {
        uart_puts("touch: parent directory not found\n");
        return 0;
    }

    if (basename[0] == '\0') {
        uart_puts("touch: missing filename\n");
        return 0;
    }

    fs_node_t *file = alloc_node(basename, FS_FILE);
    if (!file) return 0;
    add_child(parent, file);
    return file;
}

fs_node_t *fs_write(const char *path, const char *content) {
    // Create file if it doesn't exist
    fs_node_t *file = fs_resolve(path);
    if (!file) {
        file = fs_touch(path);
        if (!file) return 0;
    }

    if (file->type != FS_FILE) {
        uart_puts("write: not a file\n");
        return 0;
    }

    // Free old content
    if (file->data) {
        kfree(file->data);
        file->data = 0;
        file->size = 0;
    }

    unsigned long len = fs_strlen(content);
    if (len > FS_MAX_DATA) len = FS_MAX_DATA;

    if (len > 0) {
        file->data = (char *)kmalloc(len + 1);
        if (!file->data) {
            uart_puts("write: allocation failed\n");
            return 0;
        }
        for (unsigned long i = 0; i < len; i++)
            file->data[i] = content[i];
        file->data[len] = '\0';
        file->size = len;
    }

    return file;
}

const char *fs_read(const char *path, unsigned long *size) {
    fs_node_t *file = fs_resolve(path);
    if (!file) return 0;
    if (file->type != FS_FILE) return 0;
    if (size) *size = file->size;
    return file->data;
}

int fs_rm(const char *path) {
    fs_node_t *node = fs_resolve(path);
    if (!node) {
        uart_puts("rm: not found\n");
        return -1;
    }
    if (node->type == FS_DIR) {
        uart_puts("rm: is a directory (use rmdir)\n");
        return -1;
    }
    if (node == root) {
        uart_puts("rm: cannot remove root\n");
        return -1;
    }
    remove_child(node->parent, node);
    free_node(node);
    return 0;
}

void fs_ls(const char *path) {
    fs_node_t *dir;
    if (!path || path[0] == '\0')
        dir = cwd;
    else
        dir = fs_resolve(path);

    if (!dir) {
        uart_puts("ls: not found\n");
        return;
    }

    if (dir->type == FS_FILE) {
        // ls on a file: just show the file
        uart_puts(dir->name);
        uart_puts("  (");
        uart_put_dec(dir->size);
        uart_puts(" bytes)\n");
        return;
    }

    // List children
    fs_node_t *child = dir->children;
    if (!child) {
        uart_puts("(empty)\n");
        return;
    }

    while (child) {
        if (child->type == FS_DIR) {
            uart_puts("  ");
            uart_puts(child->name);
            uart_puts("/\n");
        } else {
            uart_puts("  ");
            uart_puts(child->name);
            uart_puts("  (");
            uart_put_dec(child->size);
            uart_puts(" bytes)\n");
        }
        child = child->next_sibling;
    }
}

void fs_get_path(fs_node_t *node, char *buf, int bufsize) {
    if (!node || bufsize < 2) { buf[0] = '\0'; return; }

    // Build path by walking up to root
    // Stack of components (max depth ~16)
    const char *parts[16];
    int depth = 0;

    fs_node_t *cur = node;
    while (cur && cur != root && depth < 16) {
        parts[depth++] = cur->name;
        cur = cur->parent;
    }

    int pos = 0;
    buf[pos++] = '/';

    for (int i = depth - 1; i >= 0; i--) {
        const char *p = parts[i];
        while (*p && pos < bufsize - 2) {
            buf[pos++] = *p++;
        }
        if (i > 0 && pos < bufsize - 2) {
            buf[pos++] = '/';
        }
    }
    buf[pos] = '\0';
}
