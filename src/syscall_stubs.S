// syscall_stubs.S - EL0 user-mode helpers
//
// Provides a return trampoline for EL0 tasks: if a user task's entry
// function returns (falls through), LR brings execution here, which
// issues a SYS_EXIT syscall to cleanly terminate the task.

.section .text

.global user_task_exit_stub
user_task_exit_stub:
    mov     x8, #2          // SYS_EXIT = 2
    svc     #0
    b       .               // Should never reach here
