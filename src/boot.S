// boot.S - Startup code for Raspberry Pi 4 with interrupt support

.section ".text.boot"

.global _start

_start:
    // Get CPU ID - only CPU 0 should continue, others should halt
    mrs     x1, mpidr_el1
    and     x1, x1, #3
    cbz     x1, cpu0_start
    
cpu_halt:
    // Put other CPUs to sleep
    wfe
    b       cpu_halt

cpu0_start:
    // Set stack pointer before calling C code
    // Stack grows downward from 0x80000
    ldr     x1, =_start
    mov     sp, x1

    // Set up exception vector table
    ldr     x1, =vectors
    msr     vbar_el1, x1

    // Enable IRQ interrupts (clear the I bit in DAIF)
    msr     daifclr, #2

    // Clear BSS section (uninitialized data)
    ldr     x1, =__bss_start
    ldr     w2, =__bss_size
bss_clear_loop:
    cbz     w2, bss_clear_done
    str     xzr, [x1], #8
    sub     w2, w2, #1
    cbnz    w2, bss_clear_loop

bss_clear_done:
    // Jump to C kernel
    bl      kernel_main
    
    // If kernel_main returns, halt
    b       cpu_halt
