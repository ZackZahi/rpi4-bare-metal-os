// vectors.S - Exception vector table for AArch64
//
// The IRQ stub saves ALL registers + ELR_EL1 + SPSR_EL1 onto the
// current task's stack (a "trapframe"), then calls the C handler
// with a pointer to the saved SP. The C handler can modify this
// pointer to switch to a different task's stack. On return, we
// restore from whatever SP the C handler gave us.
//
// Trapframe layout (34 x 8 = 272 bytes):
//   sp[0]  = x0
//   sp[1]  = x1
//   ...
//   sp[30] = x30
//   sp[31] = ELR_EL1  (return address)
//   sp[32] = SPSR_EL1 (saved processor state)
//   sp[33] = padding   (keep 16-byte alignment)

.section ".text.vectors"

.global vectors

.align 11
vectors:
    // Current EL with SP0
    .align 7
    b       hang        // Synchronous
    .align 7
    b       hang        // IRQ
    .align 7
    b       hang        // FIQ
    .align 7
    b       hang        // SError

    // Current EL with SPx  <-- this is us (EL1 using SP_EL1)
    .align 7
    b       hang        // Synchronous
    .align 7
    b       irq_entry   // IRQ
    .align 7
    b       hang        // FIQ
    .align 7
    b       hang        // SError

    // Lower EL using AArch64
    .align 7
    b       hang
    .align 7
    b       hang
    .align 7
    b       hang
    .align 7
    b       hang

    // Lower EL using AArch32
    .align 7
    b       hang
    .align 7
    b       hang
    .align 7
    b       hang
    .align 7
    b       hang

hang:
    wfe
    b       hang

// ---- IRQ entry point ----
irq_entry:
    // Allocate trapframe (34 * 8 = 272 bytes)
    sub     sp, sp, #272

    // Save general-purpose registers
    stp     x0,  x1,  [sp, #(0*8)]
    stp     x2,  x3,  [sp, #(2*8)]
    stp     x4,  x5,  [sp, #(4*8)]
    stp     x6,  x7,  [sp, #(6*8)]
    stp     x8,  x9,  [sp, #(8*8)]
    stp     x10, x11, [sp, #(10*8)]
    stp     x12, x13, [sp, #(12*8)]
    stp     x14, x15, [sp, #(14*8)]
    stp     x16, x17, [sp, #(16*8)]
    stp     x18, x19, [sp, #(18*8)]
    stp     x20, x21, [sp, #(20*8)]
    stp     x22, x23, [sp, #(22*8)]
    stp     x24, x25, [sp, #(24*8)]
    stp     x26, x27, [sp, #(26*8)]
    stp     x28, x29, [sp, #(28*8)]
    str     x30,       [sp, #(30*8)]

    // Save ELR_EL1 and SPSR_EL1
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #(31*8)]

    // Call C handler: irq_handler_c(sp)
    // x0 = pointer to trapframe (also current SP)
    mov     x0, sp
    bl      irq_handler_c
    // Returns new SP in x0 (may be same or different task)

    // Switch to returned SP (might be a different task)
    mov     sp, x0

    // Restore ELR_EL1 and SPSR_EL1
    ldp     x0, x1, [sp, #(31*8)]
    msr     elr_el1, x0
    msr     spsr_el1, x1

    // Restore general-purpose registers
    ldp     x0,  x1,  [sp, #(0*8)]
    ldp     x2,  x3,  [sp, #(2*8)]
    ldp     x4,  x5,  [sp, #(4*8)]
    ldp     x6,  x7,  [sp, #(6*8)]
    ldp     x8,  x9,  [sp, #(8*8)]
    ldp     x10, x11, [sp, #(10*8)]
    ldp     x12, x13, [sp, #(12*8)]
    ldp     x14, x15, [sp, #(14*8)]
    ldp     x16, x17, [sp, #(16*8)]
    ldp     x18, x19, [sp, #(18*8)]
    ldp     x20, x21, [sp, #(20*8)]
    ldp     x22, x23, [sp, #(22*8)]
    ldp     x24, x25, [sp, #(24*8)]
    ldp     x26, x27, [sp, #(26*8)]
    ldp     x28, x29, [sp, #(28*8)]
    ldr     x30,       [sp, #(30*8)]

    // Deallocate trapframe
    add     sp, sp, #272

    // Return from exception
    eret
