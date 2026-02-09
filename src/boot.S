// boot.S - Startup code for Raspberry Pi 4
//
// Memory layout:
//   0x80000          = _start (kernel code begins)
//   ...              = .text, .rodata, .data
//   __bss_start      = start of BSS
//   __bss_end        = end of BSS
//   __bss_end + 64KB = stack top (grows downward)
//
// IRQs are NOT enabled here. kernel_main enables them after
// GIC, timer, and memory are all initialized.

.section ".text.boot"

.global _start

_start:
    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbnz    x1, cpu_halt

    // Check current exception level
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.eq    at_el2
    cmp     x0, #1
    b.eq    at_el1
    b       cpu_halt

at_el2:
    // Enable AArch64 at EL1
    mrs     x0, hcr_el2
    orr     x0, x0, #(1 << 31)
    msr     hcr_el2, x0

    // Allow EL1 to access timers
    mov     x0, #3
    msr     cnthctl_el2, x0
    msr     cntvoff_el2, xzr

    msr     vttbr_el2, xzr

    // SPSR: EL1h, all exceptions masked
    mov     x0, #0x3c5
    msr     spsr_el2, x0

    adr     x0, at_el1
    msr     elr_el2, x0
    eret

at_el1:
    // Clear BSS (no stack needed, pure register ops)
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
bss_clear_loop:
    cmp     x1, x2
    b.ge    bss_clear_done
    str     xzr, [x1], #8
    b       bss_clear_loop

bss_clear_done:
    // Set stack pointer ABOVE BSS + 64KB
    ldr     x1, =__bss_end
    add     x1, x1, #15
    and     x1, x1, #~15
    add     x1, x1, #(64 * 1024)
    mov     sp, x1

    // Set up exception vector table
    ldr     x1, =vectors
    msr     vbar_el1, x1

    // DO NOT enable IRQs here - kernel_main will do it
    // after GIC and timer are properly initialized

    // Jump to C kernel
    bl      kernel_main

cpu_halt:
    wfe
    b       cpu_halt
