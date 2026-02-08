// context.S - Context switching for task scheduler
//
// AAPCS64 calling convention: only callee-saved registers need saving
// across function calls. These are x19-x28, x29 (FP), x30 (LR).
// The stack pointer is saved/restored via the task struct.
//
// Stack layout after save (growing downward):
//   SP+80: x29, x30    (frame pointer, link register)
//   SP+64: x27, x28
//   SP+48: x25, x26
//   SP+32: x23, x24
//   SP+16: x21, x22
//   SP+0:  x19, x20

.global switch_to

// void switch_to(unsigned long *old_sp, unsigned long new_sp)
//   x0 = pointer to old task's saved SP (NULL if first switch)
//   x1 = new task's SP to restore
switch_to:
    // If old_sp is NULL, skip saving (first context switch)
    cbz     x0, .Lrestore

    // Save callee-saved registers onto current stack
    stp     x19, x20, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!
    stp     x25, x26, [sp, #-16]!
    stp     x27, x28, [sp, #-16]!
    stp     x29, x30, [sp, #-16]!

    // Save current SP into *old_sp
    mov     x2, sp
    str     x2, [x0]

.Lrestore:
    // Switch to new task's stack
    mov     sp, x1

    // Restore callee-saved registers (reverse order of save)
    ldp     x29, x30, [sp], #16
    ldp     x27, x28, [sp], #16
    ldp     x25, x26, [sp], #16
    ldp     x23, x24, [sp], #16
    ldp     x21, x22, [sp], #16
    ldp     x19, x20, [sp], #16

    // Return to the address in x30 (LR)
    // For a new task, this will be task_wrapper
    // For a resumed task, this returns to where schedule() called switch_to
    ret
