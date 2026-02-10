// mmu.h - Memory Management Unit
//
// AArch64 MMU with 4KB granule, 48-bit VA
// Uses 2MB block mappings (L0 → L1 → L2 block descriptors)
//
// Memory attributes:
//   - Normal (cacheable, write-back) for RAM
//   - Device-nGnRnE for MMIO (UART, GIC, timers, etc.)

#ifndef MMU_H
#define MMU_H

// Initialize page tables and enable the MMU
// Must be called early, before any cache-sensitive operations
void mmu_init(void);

// Query MMU state
int mmu_is_enabled(void);

// Print MMU configuration (for shell 'mmu' command)
void mmu_dump_config(void);

#endif // MMU_H
