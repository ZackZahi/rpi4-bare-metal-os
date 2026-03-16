// mmu.c - AArch64 MMU setup for Raspberry Pi 4
//
// Identity mapping: virtual address == physical address
// Uses 4KB granule with 2MB block descriptors at level 2.
//
// Translation: VA → L0 table → L1 table → L2 block (2MB)
//
// Memory map (1GB RAM + peripherals):
//   0x00000000 - 0x3FFFFFFF  : RAM (1GB) — Normal, cacheable
//   0x40000000 - 0xFBFFFFFF  : unmapped
//   0xFC000000 - 0xFFFFFFFF  : Peripherals — Device memory
//     0xFE000000 : BCM2711 peripherals (UART, GPIO, etc.)
//     0xFF800000 : ARM Local peripherals (timer IRQ routing)
//     0xFF840000 : GIC-400
//
// Page table structure (4KB granule, 39-bit VA, T0SZ=25):
//   L1: 512 entries, each covers 1GB  — we use entries 0 and 3
//   L2: 512 entries, each covers 2MB  — block descriptors
//
// Using T0SZ=25 (39-bit VA, 3-level walk: L1→L2→block).
// All addresses we use fit in 39 bits, so we don't need the L0 level.
//
// We need:
//   1 x L1 table (4KB)
//   1 x L2 table for RAM region   (4KB) — maps 0x00000000-0x3FFFFFFF
//   1 x L2 table for device region (4KB) — maps 0xC0000000-0xFFFFFFFF
// Total: 12KB of page tables

#include "mmu.h"
#include "uart.h"

// ---- Page table entry bits ----

#define PT_VALID        (1UL << 0)
#define PT_TABLE        (1UL << 1)    // For L0/L1: points to next-level table
#define PT_BLOCK        (0UL << 1)    // For L1/L2: 1GB/2MB block mapping
#define PT_PAGE         (1UL << 1)    // For L3: 4KB page

// Access flag (must be set or we get access fault)
#define PT_AF           (1UL << 10)

// Shareability
#define PT_ISH          (3UL << 8)    // Inner shareable
#define PT_OSH          (2UL << 8)    // Outer shareable

// Access permissions
#define PT_AP_RW_EL1    (0UL << 6)    // EL1 read/write, EL0 no access
#define PT_AP_RW_ALL    (1UL << 6)    // EL1+EL0 read/write
#define PT_AP_RO_EL1    (2UL << 6)    // EL1 read-only
#define PT_AP_RO_ALL    (3UL << 6)    // EL1+EL0 read-only

// Attribute index (selects from MAIR_EL1)
#define PT_ATTR(idx)    ((unsigned long)(idx) << 2)

// Our MAIR indices:
//   0 = Device-nGnRnE (0x00)
//   1 = Normal, cacheable, write-back (0xFF)
#define MT_DEVICE       0
#define MT_NORMAL       1

// Convenience macros for block descriptors
#define BLOCK_DEVICE    (PT_VALID | PT_BLOCK | PT_AF | PT_ATTR(MT_DEVICE) | PT_OSH | PT_AP_RW_EL1)
#define BLOCK_NORMAL    (PT_VALID | PT_BLOCK | PT_AF | PT_ATTR(MT_NORMAL) | PT_ISH | PT_AP_RW_ALL)
#define TABLE_ENTRY     (PT_VALID | PT_TABLE)

// ---- Page tables (4KB-aligned, in BSS) ----
// Using __attribute__((aligned)) puts them in BSS with proper alignment

static unsigned long l1_table[512] __attribute__((aligned(4096)));
static unsigned long l2_ram_table[512] __attribute__((aligned(4096)));
static unsigned long l2_dev_table[512] __attribute__((aligned(4096)));

static int mmu_enabled = 0;

// ---- MMU initialization ----

void mmu_init(void) {
    uart_puts("  Setting up page tables...\n");

    // Zero all tables
    for (int i = 0; i < 512; i++) {
        l1_table[i] = 0;
        l2_ram_table[i] = 0;
        l2_dev_table[i] = 0;
    }

    // ---- L2 RAM table: map 0x00000000 - 0x3FFFFFFF (1GB) as Normal ----
    // 512 entries × 2MB = 1GB
    for (int i = 0; i < 512; i++) {
        unsigned long phys = (unsigned long)i * (2UL * 1024 * 1024);
        l2_ram_table[i] = phys | BLOCK_NORMAL;
    }

    // ---- L2 Device table: map 0xC0000000 - 0xFFFFFFFF (1GB) as Device ----
    // This covers all BCM2711 peripherals, ARM local periphs, GIC
    for (int i = 0; i < 512; i++) {
        unsigned long phys = 0xC0000000UL + (unsigned long)i * (2UL * 1024 * 1024);
        l2_dev_table[i] = phys | BLOCK_DEVICE;
    }

    // ---- L1 table: 512 entries, each covers 1GB ----
    // Entry 0: points to l2_ram_table  (covers 0x00000000 - 0x3FFFFFFF)
    // Entry 3: points to l2_dev_table  (covers 0xC0000000 - 0xFFFFFFFF)
    l1_table[0] = (unsigned long)l2_ram_table | TABLE_ENTRY;
    l1_table[3] = (unsigned long)l2_dev_table | TABLE_ENTRY;

    uart_puts("  L1 table at ");
    uart_put_hex((unsigned long)l1_table);
    uart_puts("\n");

    // Compiler barrier: force all page table stores to complete before
    // any system register writes. GCC -O2 can sink stores to static
    // arrays past asm volatile unless we emit a "memory" clobber here.
    asm volatile("" ::: "memory");

    // ---- Configure MAIR_EL1 (Memory Attribute Indirection Register) ----
    // Attr0 = 0x00: Device-nGnRnE
    // Attr1 = 0xFF: Normal, Write-Back, Read-Allocate, Write-Allocate (inner+outer)
    unsigned long mair = (0x00UL << (MT_DEVICE * 8)) |
                         (0xFFUL << (MT_NORMAL * 8));
    asm volatile("msr mair_el1, %0" :: "r"(mair));

    // ---- Configure TCR_EL1 (Translation Control Register) ----
    // T0SZ = 25  → 39-bit VA space (2^39 = 512GB), 3-level walk: L1→L2→block
    // IRGN0 = 01 → Normal, inner write-back write-allocate cacheable
    // ORGN0 = 01 → Normal, outer write-back write-allocate cacheable
    // SH0 = 11   → Inner shareable
    // TG0 = 00   → 4KB granule for TTBR0
    // EPD1 = 1   → Disable TTBR1 table walks (we don't use upper VA range)
    // T1SZ = 25  → (unused since EPD1=1, but must be valid)
    // TG1 = 10   → 4KB granule for TTBR1 (must be valid even if EPD1=1)
    // IPS = 000  → 32-bit physical address space (4GB) — all our PAs fit
    unsigned long tcr = (25UL << 0)   |  // T0SZ = 25 (39-bit VA, 3-level walk)
                        (1UL  << 8)   |  // IRGN0 = write-back
                        (1UL  << 10)  |  // ORGN0 = write-back
                        (3UL  << 12)  |  // SH0 = inner shareable
                        (0UL  << 14)  |  // TG0 = 4KB
                        (25UL << 16)  |  // T1SZ = 25 (unused, EPD1=1)
                        (1UL  << 23)  |  // EPD1 = disable TTBR1 walks
                        (2UL  << 30)  |  // TG1 = 4KB (valid granule)
                        (0UL  << 32);    // IPS = 000 = 32-bit PA (4GB)
    asm volatile("msr tcr_el1, %0" :: "r"(tcr));

    // ---- Set TTBR0_EL1 to our L1 table (3-level walk starts at L1) ----
    asm volatile("msr ttbr0_el1, %0" :: "r"((unsigned long)l1_table));
    // Clear TTBR1 (not used, EPD1=1 disables its walks)
    asm volatile("msr ttbr1_el1, %0" :: "r"(0UL));

    // Hardware + compiler barrier: ensure all writes are visible before TLB ops
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb");

    // Invalidate all TLB entries
    asm volatile("tlbi vmalle1is");
    asm volatile("dsb ish" ::: "memory");
    asm volatile("isb");

    uart_puts("  Enabling MMU...\n");

    // ---- Enable MMU via SCTLR_EL1 ----
    unsigned long sctlr;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1UL << 0);     // M = MMU on
    sctlr |= (1UL << 2);     // C = data cache on
    sctlr |= (1UL << 12);    // I = instruction cache on
    // Note: do NOT clear bits 22/23 — they are RES1 on ARMv8.0/Cortex-A72
    asm volatile(
        "ic  iallu\n"         // Invalidate all instruction caches
        "dsb ish\n"
        "isb\n"
        "msr sctlr_el1, %0\n"
        "isb\n"
        :: "r"(sctlr) : "memory"
    );

    mmu_enabled = 1;
    uart_puts("  MMU enabled! Identity-mapped with caches on.\n");
}

int mmu_is_enabled(void) {
    return mmu_enabled;
}

void mmu_dump_config(void) {
    unsigned long sctlr, tcr, mair, ttbr0;
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    asm volatile("mrs %0, tcr_el1"   : "=r"(tcr));
    asm volatile("mrs %0, mair_el1"  : "=r"(mair));
    asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));

    uart_puts("MMU Configuration:\n");
    uart_puts("  SCTLR_EL1: ");
    uart_put_hex(sctlr);
    uart_puts("\n");
    uart_puts("    MMU:    ");
    uart_puts((sctlr & 1) ? "ON" : "OFF");
    uart_puts("\n    D-Cache: ");
    uart_puts((sctlr & (1 << 2)) ? "ON" : "OFF");
    uart_puts("\n    I-Cache: ");
    uart_puts((sctlr & (1 << 12)) ? "ON" : "OFF");
    uart_puts("\n");

    uart_puts("  TCR_EL1:   ");
    uart_put_hex(tcr);
    uart_puts("\n");
    uart_puts("    T0SZ:   ");
    uart_put_dec(tcr & 0x3F);
    uart_puts(" (");
    uart_put_dec(64 - (tcr & 0x3F));
    uart_puts("-bit VA)\n");

    unsigned long ips = (tcr >> 32) & 0x7;
    static const char *ips_names[] = {
        "32-bit (4GB)", "36-bit (64GB)", "40-bit (1TB)",
        "42-bit (4TB)", "44-bit (16TB)", "48-bit (256TB)"
    };
    uart_puts("    IPS:    ");
    if (ips < 6) uart_puts(ips_names[ips]);
    else uart_put_dec(ips);
    uart_puts("\n");

    uart_puts("  MAIR_EL1:  ");
    uart_put_hex(mair);
    uart_puts("\n");
    uart_puts("    Attr0:  0x");
    uart_put_hex(mair & 0xFF);
    uart_puts(" (Device)\n");
    uart_puts("    Attr1:  0x");
    uart_put_hex((mair >> 8) & 0xFF);
    uart_puts(" (Normal)\n");

    uart_puts("  TTBR0_EL1: ");
    uart_put_hex(ttbr0);
    uart_puts("\n");

    // Show mapping summary
    uart_puts("\nMemory map:\n");
    uart_puts("  0x00000000-0x3FFFFFFF  1GB RAM    (Normal, cacheable)\n");
    uart_puts("  0xC0000000-0xFFFFFFFF  1GB Device (UART, GIC, timers)\n");

    uart_puts("\nPage tables: ");
    uart_put_dec(3 * 4);
    uart_puts(" KB (3 tables x 4KB)\n");
}
