// boot.S - Startup code for Raspberry Pi 4
//
// QEMU raspi4b boots the kernel at EL2. We need to:
//   1. Check current EL
//   2. If EL2, configure EL1 and drop down to it
//   3. Set up stack, vectors, BSS, then jump to C

.section ".text.boot"

.global _start

_start:
    // Only CPU 0 proceeds; other cores halt
    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbnz    x1, cpu_halt

    // Check which EL we're at
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.eq    at_el2
    cmp     x0, #1
    b.eq    at_el1
    // EL3 or EL0 — shouldn't happen, but halt
    b       cpu_halt

at_el2:
    // ---- Configure EL1 from EL2 ----

    // Enable AArch64 in EL1 (HCR_EL2.RW = 1)
    mrs     x0, hcr_el2
    orr     x0, x0, #(1 << 31)     // RW bit = AArch64
    msr     hcr_el2, x0

    // Don't trap timer access to EL2 (CNTHCTL_EL2)
    // Set EL1PCTEN and EL1PCEN so EL1 can access timers
    mov     x0, #3
    msr     cnthctl_el2, x0
    // Clear virtual offset
    msr     cntvoff_el2, xzr

    // No hypervisor traps
    msr     vttbr_el2, xzr

    // Set up SPSR for EL1h (EL1 using SP_EL1)
    // SPSR_EL2: M[3:0] = 0b0101 = EL1h, DAIF all masked
    mov     x0, #0x3c5
    msr     spsr_el2, x0

    // Set the return address to at_el1
    adr     x0, at_el1
    msr     elr_el2, x0

    // Drop to EL1
    eret

at_el1:
    // ---- Now running at EL1 ----

    // Set stack pointer (grows down from _start = 0x80000)
    ldr     x1, =_start
    mov     sp, x1

    // Set up exception vector table at EL1
    ldr     x1, =vectors
    msr     vbar_el1, x1

    // Enable IRQ interrupts (clear I bit in DAIF)
    msr     daifclr, #2

    // Clear BSS
    ldr     x1, =__bss_start
    ldr     w2, =__bss_size
bss_clear_loop:
    cbz     w2, bss_clear_done
    str     xzr, [x1], #8
    sub     w2, w2, #1
    b       bss_clear_loop

bss_clear_done:
    // Jump to C kernel
    bl      kernel_main

    // If kernel returns, halt
cpu_halt:
    wfe
    b       cpu_halt
