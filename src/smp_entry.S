// smp_entry.S - Secondary core startup trampoline
//
// QEMU raspi4b boots secondary cores at EL2. They spin checking
// addresses 0xE0/0xE8/0xF0. When we write our entry point there,
// they jump here.
//
// This code:
//   1. Drops from EL2 to EL1
//   2. Sets up the exception vector table
//   3. Enables the MMU with core 0's page tables
//   4. Sets up a per-core stack
//   5. Calls secondary_core_main() in C

.section ".text"

.global secondary_entry
.global smp_stacks
.global smp_shared_ttbr0
.global smp_shared_tcr
.global smp_shared_mair
.extern vectors
.extern secondary_core_main

secondary_entry:
    // We're at EL2 — configure EL1 and drop down

    // Enable AArch64 at EL1
    mrs     x0, hcr_el2
    orr     x0, x0, #(1 << 31)
    msr     hcr_el2, x0

    // Allow EL1 timer access
    mov     x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr
    msr     vttbr_el2, xzr

    // SPSR: EL1h, all exceptions masked
    mov     x0, #0x3c5
    msr     spsr_el2, x0

    // Return to el1_setup
    adr     x0, el1_setup
    msr     elr_el2, x0
    eret

el1_setup:
    // Now at EL1 — get core ID
    mrs     x19, mpidr_el1
    and     x19, x19, #3            // x19 = core_id (1, 2, or 3)

    // Set exception vector table
    ldr     x0, =vectors
    msr     vbar_el1, x0

    // Set up MMU using core 0's page tables (shared, read from memory)
    ldr     x0, =smp_shared_ttbr0
    ldr     x0, [x0]
    msr     ttbr0_el1, x0
    msr     ttbr1_el1, xzr

    ldr     x0, =smp_shared_mair
    ldr     x0, [x0]
    msr     mair_el1, x0

    ldr     x0, =smp_shared_tcr
    ldr     x0, [x0]
    msr     tcr_el1, x0

    dsb     ish
    isb

    // Enable MMU + caches
    mrs     x0, sctlr_el1
    orr     x0, x0, #(1 << 0)      // M = MMU on
    orr     x0, x0, #(1 << 2)      // C = D-cache
    orr     x0, x0, #(1 << 12)     // I = I-cache
    msr     sctlr_el1, x0
    isb

    // Set up per-core stack
    // smp_stacks[core_id] has the stack top address
    ldr     x0, =smp_stacks
    lsl     x1, x19, #3            // x1 = core_id * 8
    ldr     x0, [x0, x1]           // x0 = smp_stacks[core_id]
    mov     sp, x0

    // Call C entry point with core_id as argument
    mov     x0, x19
    bl      secondary_core_main

    // Should never return
halt_secondary:
    wfe
    b       halt_secondary
