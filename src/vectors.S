// vectors.S - Exception vector table for AArch64
//
// Handles:
//   - IRQs from EL1 (current EL with SPx) — timer-driven preemptive scheduling
//   - IRQs from EL0 (lower EL AArch64) — preempt user-mode tasks
//   - Sync from EL0 (lower EL AArch64) — syscall dispatch (svc #0)
//
// Trapframe layout (34 x 8 = 272 bytes):
//   sp[0]  = x0   ... sp[30] = x30
//   sp[31] = ELR_EL1  (return address)
//   sp[32] = SPSR_EL1 (saved processor state)
//   sp[33] = padding   (16-byte alignment)

.section ".text.vectors"

.global vectors

.align 11
vectors:
    // Current EL with SP0  (offset 0x000)
    .align 7
    b       hang        // Synchronous
    .align 7
    b       hang        // IRQ
    .align 7
    b       hang        // FIQ
    .align 7
    b       hang        // SError

    // Current EL with SPx  (offset 0x200) — EL1 kernel mode
    .align 7
    b       el1_sync_entry   // Synchronous — catch SVC from EL1 tasks too
    .align 7
    b       irq_entry        // IRQ — timer preemption for kernel tasks
    .align 7
    b       hang        // FIQ
    .align 7
    b       hang        // SError

    // Lower EL using AArch64  (offset 0x400) — EL0 user mode
    .align 7
    b       el0_sync_entry   // Synchronous — syscalls (svc #0)
    .align 7
    b       el0_irq_entry    // IRQ — preempt user tasks
    .align 7
    b       hang             // FIQ
    .align 7
    b       hang             // SError

    // Lower EL using AArch32  (offset 0x600)
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

// ==================================================================
// Macro: save trapframe (272 bytes) onto current SP (SP_EL1)
// ==================================================================
.macro SAVE_TRAPFRAME
    sub     sp, sp, #272
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
    mrs     x0, elr_el1
    mrs     x1, spsr_el1
    stp     x0, x1, [sp, #(31*8)]
.endm

// ==================================================================
// Macro: restore trapframe from current SP, then eret
// ==================================================================
.macro RESTORE_TRAPFRAME
    ldp     x0, x1, [sp, #(31*8)]
    msr     elr_el1, x0
    msr     spsr_el1, x1
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
    add     sp, sp, #272
    eret
.endm

// ==================================================================
// IRQ from EL1 (kernel tasks, shell) — same as before
// ==================================================================
irq_entry:
    SAVE_TRAPFRAME

    mov     x0, sp
    bl      irq_handler_c
    mov     sp, x0

    RESTORE_TRAPFRAME

// ==================================================================
// IRQ from EL0 (user-mode tasks)
//
// When an IRQ occurs in EL0, the CPU automatically switches to SP_EL1.
// We save the trapframe on the kernel stack (SP_EL1), handle the IRQ,
// then restore — possibly to a different task.
// ==================================================================
el0_irq_entry:
    SAVE_TRAPFRAME

    mov     x0, sp
    bl      irq_handler_c
    mov     sp, x0

    RESTORE_TRAPFRAME

// ==================================================================
// Sync exception from EL0 — syscall handler
//
// Check ESR_EL1.EC for SVC from AArch64 (EC=0x15).
// ==================================================================
.extern syscall_dispatch
.extern el0_sync_fault_handler

el0_sync_entry:
    SAVE_TRAPFRAME

    // Check exception class
    mrs     x0, esr_el1
    lsr     x0, x0, #26           // EC field = bits [31:26]
    cmp     x0, #0x15             // SVC from AArch64?
    b.ne    el0_sync_unknown

    // Syscall dispatch
    mov     x0, sp
    bl      syscall_dispatch
    mov     sp, x0

    RESTORE_TRAPFRAME

el0_sync_unknown:
    // Unknown sync exception — kill the faulting task
    mov     x0, sp
    bl      el0_sync_fault_handler
    mov     sp, x0
    RESTORE_TRAPFRAME

// ==================================================================
// Sync exception from EL1 — SVC from kernel-mode tasks
// Same logic: check for SVC, dispatch syscall
// ==================================================================
el1_sync_entry:
    SAVE_TRAPFRAME

    mrs     x0, esr_el1
    lsr     x0, x0, #26
    cmp     x0, #0x15             // SVC from AArch64?
    b.ne    el1_sync_unknown

    mov     x0, sp
    bl      syscall_dispatch
    mov     sp, x0

    RESTORE_TRAPFRAME

el1_sync_unknown:
    // Unknown EL1 sync — fatal, just hang with debug
    mov     x0, sp
    bl      el0_sync_fault_handler
    mov     sp, x0
    RESTORE_TRAPFRAME
