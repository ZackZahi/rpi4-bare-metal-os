// memory.h - Memory allocator
//
// Two-level allocator:
//   1. Page allocator — manages 4KB pages via bitmap
//   2. kmalloc/kfree  — sub-page allocations from a simple block allocator
//
// Memory layout (QEMU raspi4b with 1GB RAM):
//   0x00000000 - 0x0007FFFF  : Reserved (firmware, boot)
//   0x00080000 - kernel end  : Kernel code + data + BSS
//   kernel end - 0x003FFFFF  : Kernel heap (managed by allocator)
//   0x00400000+              : Free pages
//   ...
//   0x3FFFFFFF               : End of 1GB RAM

#ifndef MEMORY_H
#define MEMORY_H

#define PAGE_SIZE       4096
#define PAGE_SHIFT      12

// Initialize the memory allocator
// Call once after BSS is cleared
void memory_init(void);

// Page allocator: allocate/free 4KB-aligned pages
void *page_alloc(void);                  // Returns NULL on failure
void *page_alloc_n(unsigned int count);  // Allocate contiguous pages
void page_free(void *addr);
void page_free_n(void *addr, unsigned int count);

// kmalloc/kfree: general-purpose allocator for small allocations
void *kmalloc(unsigned long size);
void kfree(void *ptr);

// Stats
unsigned long memory_get_total_pages(void);
unsigned long memory_get_free_pages(void);
unsigned long memory_get_used_pages(void);

#endif // MEMORY_H
