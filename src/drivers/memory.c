// memory.c - Physical page allocator and kmalloc/kfree
//
// Layout:
//   0x00080000 - ~0x000A4000  : Kernel + BSS + stack
//   0x00100000 - 0x00100800   : Bitmap (2KB)
//   0x00101000+               : Free pages (heap + allocatable)

#include "memory.h"
#include "uart.h"

#define MANAGED_SIZE    (64UL * 1024 * 1024)
#define MANAGED_PAGES   (MANAGED_SIZE / PAGE_SIZE)
#define BITMAP_SIZE     (MANAGED_PAGES / 8)

// Place bitmap at 1MB — safely above kernel region
#define BITMAP_ADDR     0x100000UL

static unsigned char *page_bitmap = (unsigned char *)BITMAP_ADDR;

static unsigned long first_free_page = 0;
static unsigned long total_pages = MANAGED_PAGES;
static unsigned long used_pages = 0;

static inline void bitmap_set(unsigned long local) {
    if (local < MANAGED_PAGES)
        page_bitmap[local / 8] |= (1 << (local % 8));
}

static inline void bitmap_clear(unsigned long local) {
    if (local < MANAGED_PAGES)
        page_bitmap[local / 8] &= ~(1 << (local % 8));
}

static inline int bitmap_test(unsigned long local) {
    if (local >= MANAGED_PAGES) return 1;
    return (page_bitmap[local / 8] >> (local % 8)) & 1;
}

typedef struct block_header {
    unsigned long size;
    unsigned long magic;
    struct block_header *next;
    int is_page_alloc;
} block_header_t;

#define BLOCK_MAGIC     0xDEADBEEFUL
#define HEADER_SIZE     sizeof(block_header_t)

#define HEAP_PAGES      64
#define HEAP_SIZE       (HEAP_PAGES * PAGE_SIZE)

static unsigned char *heap_start = 0;
static unsigned char *heap_end = 0;
static unsigned char *heap_brk = 0;
static block_header_t *free_list = 0;

void memory_init(void) {
    // Pages start after bitmap, page-aligned
    unsigned long pages_start = (BITMAP_ADDR + BITMAP_SIZE + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    first_free_page = pages_start / PAGE_SIZE;

    // Quick test: can we write to the bitmap region?
    volatile unsigned char *test = (volatile unsigned char *)BITMAP_ADDR;
    test[0] = 0xAA;
    if (test[0] != 0xAA) {
        uart_puts("  ERROR: Cannot write to bitmap at ");
        uart_put_hex(BITMAP_ADDR);
        uart_puts("!\n");
        return;
    }
    test[0] = 0;

    // Quick test: can we write to heap region?
    volatile unsigned char *test2 = (volatile unsigned char *)pages_start;
    test2[0] = 0xBB;
    if (test2[0] != 0xBB) {
        uart_puts("  ERROR: Cannot write to heap at ");
        uart_put_hex(pages_start);
        uart_puts("!\n");
        return;
    }
    test2[0] = 0;

    // Clear bitmap
    for (unsigned long i = 0; i < BITMAP_SIZE; i++)
        page_bitmap[i] = 0x00;

    used_pages = 0;

    // Reserve heap pages
    heap_start = (unsigned char *)pages_start;
    for (unsigned long i = 0; i < HEAP_PAGES; i++) {
        bitmap_set(i);
        used_pages++;
    }
    heap_end = heap_start + HEAP_SIZE;
    heap_brk = heap_start;
    free_list = 0;
}

void *page_alloc(void) { return page_alloc_n(1); }

void *page_alloc_n(unsigned int count) {
    if (count == 0) return 0;
    unsigned long i = 0;
    while (i + count <= total_pages) {
        int found = 1;
        for (unsigned long j = 0; j < count; j++) {
            if (bitmap_test(i + j)) { i = i + j + 1; found = 0; break; }
        }
        if (found) {
            for (unsigned long j = 0; j < count; j++) { bitmap_set(i + j); used_pages++; }
            return (void *)((first_free_page + i) * PAGE_SIZE);
        }
    }
    return 0;
}

void page_free(void *addr) { page_free_n(addr, 1); }

void page_free_n(void *addr, unsigned int count) {
    unsigned long page = (unsigned long)addr / PAGE_SIZE;
    if (page < first_free_page) return;
    unsigned long local = page - first_free_page;
    for (unsigned long i = 0; i < count; i++) {
        if (bitmap_test(local + i)) { bitmap_clear(local + i); used_pages--; }
    }
}

void *kmalloc(unsigned long size) {
    if (size == 0) return 0;
    size = (size + 15) & ~15UL;
    unsigned long total = size + HEADER_SIZE;

    if (size > PAGE_SIZE / 2) {
        unsigned int pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
        void *p = page_alloc_n(pages);
        if (!p) return 0;
        block_header_t *hdr = (block_header_t *)p;
        hdr->size = size; hdr->magic = BLOCK_MAGIC; hdr->next = 0; hdr->is_page_alloc = pages;
        return (void *)((unsigned char *)p + HEADER_SIZE);
    }

    block_header_t *prev = 0;
    block_header_t *blk = free_list;
    while (blk) {
        if (blk->size >= size) {
            if (prev) prev->next = blk->next; else free_list = blk->next;
            blk->next = 0; blk->magic = BLOCK_MAGIC;
            return (void *)((unsigned char *)blk + HEADER_SIZE);
        }
        prev = blk; blk = blk->next;
    }

    if (heap_brk + total > heap_end) {
        unsigned int pages = (total + PAGE_SIZE - 1) / PAGE_SIZE;
        void *p = page_alloc_n(pages);
        if (!p) return 0;
        block_header_t *hdr = (block_header_t *)p;
        hdr->size = size; hdr->magic = BLOCK_MAGIC; hdr->next = 0; hdr->is_page_alloc = pages;
        return (void *)((unsigned char *)p + HEADER_SIZE);
    }

    block_header_t *hdr = (block_header_t *)heap_brk;
    heap_brk += total;
    hdr->size = size; hdr->magic = BLOCK_MAGIC; hdr->next = 0; hdr->is_page_alloc = 0;
    return (void *)((unsigned char *)hdr + HEADER_SIZE);
}

void kfree(void *ptr) {
    if (!ptr) return;
    block_header_t *hdr = (block_header_t *)((unsigned char *)ptr - HEADER_SIZE);
    if (hdr->magic != BLOCK_MAGIC) { uart_puts("[kfree] bad magic\n"); return; }
    hdr->magic = 0;
    if (hdr->is_page_alloc > 0) { page_free_n((void *)hdr, hdr->is_page_alloc); return; }
    hdr->next = free_list; free_list = hdr;
}

unsigned long memory_get_total_pages(void) { return total_pages; }
unsigned long memory_get_free_pages(void)  { return total_pages - used_pages; }
unsigned long memory_get_used_pages(void)  { return used_pages; }
