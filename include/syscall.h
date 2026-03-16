// syscall.h - System call interface for EL0 user-mode tasks
//
// Syscall convention (AArch64):
//   x8 = syscall number
//   x0, x1 = arguments
//   x0 = return value
//   Invoke via: svc #0

#ifndef SYSCALL_H
#define SYSCALL_H

// Syscall numbers
#define SYS_WRITE   0   // sys_write(buf, len)
#define SYS_READ    1   // sys_read(buf, len)
#define SYS_EXIT    2   // sys_exit()
#define SYS_YIELD   3   // sys_yield()
#define SYS_SLEEP   4   // sys_sleep(ms)
#define SYS_GETPID  5   // sys_getpid()

#define NR_SYSCALLS 6

// Kernel-side dispatcher (called from vectors.S)
unsigned long syscall_dispatch(unsigned long sp);

// ---- User-side inline stubs (used by EL0 tasks) ----

static inline long sys_write(const char *buf, unsigned long len) {
    register const char *r0 __asm__("x0") = buf;
    register unsigned long r1 __asm__("x1") = len;
    register unsigned long r8 __asm__("x8") = SYS_WRITE;
    register long ret __asm__("x0");
    __asm__ volatile(
        "svc #0"
        : "=r"(ret)
        : "r"(r0), "r"(r1), "r"(r8)
        : "memory"
    );
    return ret;
}

static inline long sys_read(char *buf, unsigned long len) {
    register char *r0 __asm__("x0") = buf;
    register unsigned long r1 __asm__("x1") = len;
    register unsigned long r8 __asm__("x8") = SYS_READ;
    register long ret __asm__("x0");
    __asm__ volatile(
        "svc #0"
        : "=r"(ret)
        : "r"(r0), "r"(r1), "r"(r8)
        : "memory"
    );
    return ret;
}

static inline void sys_exit(void) {
    register unsigned long r8 __asm__("x8") = SYS_EXIT;
    __asm__ volatile("svc #0" :: "r"(r8));
    __builtin_unreachable();
}

static inline void sys_yield(void) {
    register unsigned long r8 __asm__("x8") = SYS_YIELD;
    __asm__ volatile("svc #0" :: "r"(r8) : "memory");
}

static inline void sys_sleep(unsigned int ms) {
    register unsigned long r0 __asm__("x0") = ms;
    register unsigned long r8 __asm__("x8") = SYS_SLEEP;
    __asm__ volatile("svc #0" :: "r"(r0), "r"(r8) : "memory");
}

static inline unsigned int sys_getpid(void) {
    register unsigned long r8 __asm__("x8") = SYS_GETPID;
    register unsigned long ret __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(ret) : "r"(r8));
    return (unsigned int)ret;
}

#endif // SYSCALL_H
