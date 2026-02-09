// kernel.c - Raspberry Pi 4 Bare Metal OS
// Features: UART shell, timer interrupts, preemptive scheduler, memory allocator

#include "uart.h"
#include "timer.h"
#include "gic.h"
#include "task.h"
#include "memory.h"

static volatile int scheduler_enabled = 0;

// Called from vectors.S
unsigned long irq_handler_c(unsigned long sp) {
    unsigned long ctl;
    asm volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));

    if (ctl & 0x4) {
        timer_handle_irq();

        unsigned long ticks = timer_get_tick_count();
        if (ticks % 100 == 0 && ticks > 0) {
            uart_puts("\n[Timer: ");
            uart_put_dec(ticks / 10);
            uart_puts("s]\n");
        }

        if (scheduler_enabled) {
            sp = schedule_irq(sp);
        }
    }

    return sp;
}

// ---- Utility ----

static int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static int strncmp(const char *s1, const char *s2, int n) {
    while (n > 0 && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static unsigned long parse_num(const char *s) {
    unsigned long val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

// ---- Demo tasks ----

static void task_counter(void) {
    for (int i = 1; i <= 5; i++) {
        uart_puts("[counter] ");
        uart_put_dec(i);
        uart_puts("/5\n");
        task_sleep(1000);
    }
    uart_puts("[counter] finished\n");
}

static void task_spinner(void) {
    const char spin[] = "|/-\\";
    for (int i = 0; i < 20; i++) {
        uart_puts("[spinner] ");
        uart_putc(spin[i % 4]);
        uart_puts("\n");
        task_sleep(500);
    }
    uart_puts("[spinner] finished\n");
}

static void task_memtest(void) {
    uart_puts("[memtest] Allocating buffers...\n");

    volatile char *buf1 = (volatile char *)kmalloc(64);
    volatile char *buf2 = (volatile char *)kmalloc(256);
    volatile char *buf3 = (volatile char *)kmalloc(1024);

    if (buf1 && buf2 && buf3) {
        for (int i = 0; i < 64; i++) buf1[i] = 'A';
        for (int i = 0; i < 256; i++) buf2[i] = 'B';
        for (int i = 0; i < 1024; i++) buf3[i] = 'C';

        uart_puts("[memtest] buf1(64B)=");
        uart_put_hex((unsigned long)buf1);
        uart_puts(" buf2(256B)=");
        uart_put_hex((unsigned long)buf2);
        uart_puts(" buf3(1KB)=");
        uart_put_hex((unsigned long)buf3);
        uart_puts("\n");

        // Verify
        uart_puts("[memtest] Verifying: ");
        uart_putc(buf1[0]);
        uart_putc(buf2[0]);
        uart_putc(buf3[0]);
        uart_puts(" (expect ABC)\n");

        task_sleep(2000);

        uart_puts("[memtest] Freeing buffers...\n");
        kfree((void *)buf1);
        kfree((void *)buf2);
        kfree((void *)buf3);

        uart_puts("[memtest] Allocating 4KB page...\n");
        void *page = page_alloc();
        if (page) {
            uart_puts("[memtest] Got page at ");
            uart_put_hex((unsigned long)page);
            uart_puts("\n");
            // Write to page to verify
            volatile char *p = (volatile char *)page;
            for (int i = 0; i < 4096; i++) p[i] = 'X';
            uart_puts("[memtest] Page write OK, freeing...\n");
            page_free(page);
        }
    } else {
        uart_puts("[memtest] Allocation failed!\n");
    }

    uart_puts("[memtest] Done. Free pages: ");
    uart_put_dec(memory_get_free_pages());
    uart_puts("\n");
}

// ---- Command processor ----

static const char *state_name(task_state_t s) {
    switch (s) {
        case TASK_READY:   return "READY";
        case TASK_RUNNING: return "RUNNING";
        case TASK_BLOCKED: return "BLOCKED";
        case TASK_DEAD:    return "DEAD";
        default:           return "?";
    }
}

static void process_command(char *cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    if (strcmp(cmd, "help") == 0) {
        uart_puts("Available commands:\n");
        uart_puts("  help      - Show this help message\n");
        uart_puts("  time      - Show current tick count\n");
        uart_puts("  info      - Show system information\n");
        uart_puts("  clear     - Clear screen\n");
        uart_puts("  ps        - List all tasks\n");
        uart_puts("  spawn     - Launch demo tasks (counter + spinner)\n");
        uart_puts("  memtest   - Launch memory test task\n");
        uart_puts("  mem       - Show memory statistics\n");
        uart_puts("  alloc N   - Allocate N bytes and show address\n");
        uart_puts("  pgalloc   - Allocate a 4KB page\n");
        uart_puts("  pgfree A  - Free page at hex address A\n");
        return;
    }

    if (strcmp(cmd, "time") == 0) {
        unsigned long ticks = timer_get_tick_count();
        uart_puts("Uptime: ");
        uart_put_dec(ticks / 10);
        uart_puts(" seconds (");
        uart_put_dec(ticks);
        uart_puts(" ticks)\n");
        return;
    }

    if (strcmp(cmd, "info") == 0) {
        uart_puts("Raspberry Pi 4 Bare Metal OS\n");
        uart_puts("CPU: ARM Cortex-A72 (ARMv8-A)\n");
        uart_puts("Timer frequency: ");
        uart_put_dec(timer_get_frequency());
        uart_puts(" Hz\n");
        uart_puts("Scheduler: preemptive round-robin (100ms quantum)\n");
        uart_puts("Max tasks: ");
        uart_put_dec(MAX_TASKS);
        uart_puts("\n");
        uart_puts("Total memory: ");
        uart_put_dec((memory_get_total_pages() * PAGE_SIZE) / (1024 * 1024));
        uart_puts(" MB (");
        uart_put_dec(memory_get_total_pages());
        uart_puts(" pages)\n");
        uart_puts("Free memory:  ");
        uart_put_dec((memory_get_free_pages() * PAGE_SIZE) / (1024 * 1024));
        uart_puts(" MB (");
        uart_put_dec(memory_get_free_pages());
        uart_puts(" pages)\n");
        return;
    }

    if (strcmp(cmd, "clear") == 0) {
        uart_puts("\033[2J\033[H");
        return;
    }

    if (strcmp(cmd, "ps") == 0) {
        task_t *pool = get_task_pool();
        uart_puts("ID  NAME            STATE\n");
        uart_puts("--  ----            -----\n");
        for (int i = 0; i < MAX_TASKS; i++) {
            if (pool[i].state == TASK_DEAD && pool[i].name[0] == '\0') continue;
            if (pool[i].state == TASK_DEAD && i != 0) continue;
            uart_put_dec(pool[i].id);
            if (pool[i].id < 10) uart_puts("   ");
            else uart_puts("  ");
            uart_puts(pool[i].name);
            int len = 0;
            const char *p = pool[i].name;
            while (*p++) len++;
            for (int j = len; j < 16; j++) uart_putc(' ');
            uart_puts(state_name(pool[i].state));
            if (&pool[i] == get_current_task()) uart_puts(" <-- current");
            uart_puts("\n");
        }
        return;
    }

    if (strcmp(cmd, "spawn") == 0) {
        uart_puts("Spawning 'counter' and 'spinner' tasks...\n");
        task_create(task_counter, "counter");
        task_create(task_spinner, "spinner");
        return;
    }

    if (strcmp(cmd, "memtest") == 0) {
        uart_puts("Spawning 'memtest' task...\n");
        task_create(task_memtest, "memtest");
        return;
    }

    if (strcmp(cmd, "mem") == 0) {
        uart_puts("Memory statistics:\n");
        uart_puts("  Total pages: ");
        uart_put_dec(memory_get_total_pages());
        uart_puts(" (");
        uart_put_dec((memory_get_total_pages() * PAGE_SIZE) / (1024 * 1024));
        uart_puts(" MB)\n");
        uart_puts("  Used pages:  ");
        uart_put_dec(memory_get_used_pages());
        uart_puts(" (");
        uart_put_dec((memory_get_used_pages() * PAGE_SIZE) / 1024);
        uart_puts(" KB)\n");
        uart_puts("  Free pages:  ");
        uart_put_dec(memory_get_free_pages());
        uart_puts(" (");
        uart_put_dec((memory_get_free_pages() * PAGE_SIZE) / (1024 * 1024));
        uart_puts(" MB)\n");
        return;
    }

    if (strncmp(cmd, "alloc ", 6) == 0) {
        unsigned long size = parse_num(cmd + 6);
        if (size == 0) { uart_puts("Usage: alloc <size>\n"); return; }
        void *ptr = kmalloc(size);
        if (ptr) {
            uart_puts("Allocated ");
            uart_put_dec(size);
            uart_puts(" bytes at ");
            uart_put_hex((unsigned long)ptr);
            uart_puts("\n");
        } else {
            uart_puts("Allocation failed!\n");
        }
        return;
    }

    if (strcmp(cmd, "pgalloc") == 0) {
        void *page = page_alloc();
        if (page) {
            uart_puts("Allocated page at ");
            uart_put_hex((unsigned long)page);
            uart_puts("\n");
        } else {
            uart_puts("Page allocation failed!\n");
        }
        return;
    }

    if (strncmp(cmd, "pgfree ", 7) == 0) {
        const char *s = cmd + 7;
        while (*s == ' ') s++;
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
        unsigned long addr = 0;
        while (*s) {
            addr <<= 4;
            if (*s >= '0' && *s <= '9') addr |= (*s - '0');
            else if (*s >= 'a' && *s <= 'f') addr |= (*s - 'a' + 10);
            else if (*s >= 'A' && *s <= 'F') addr |= (*s - 'A' + 10);
            else break;
            s++;
        }
        if (addr == 0) { uart_puts("Usage: pgfree <hex_address>\n"); return; }
        page_free((void *)addr);
        uart_puts("Freed page at ");
        uart_put_hex(addr);
        uart_puts("\n");
        return;
    }

    uart_puts("Unknown command: ");
    uart_puts(cmd);
    uart_puts("\nType 'help' for available commands.\n");
}

// ---- Kernel entry point ----

void kernel_main(void) {
    uart_init();

    uart_puts("\033[2J\033[H");
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  Raspberry Pi 4 OS\n");
    uart_puts("========================================\n\n");
    uart_puts("Initializing system...\n");

    uart_puts("Initializing memory allocator...\n");
    memory_init();

    uart_puts("Setting up GIC interrupt controller...\n");
    gic_init();

    unsigned long freq = timer_get_frequency();
    uart_puts("Timer frequency: ");
    uart_put_dec(freq);
    uart_puts(" Hz\n");

    uart_puts("Setting up timer interrupts (100ms interval)...\n");
    timer_init(100);
    gic_enable_interrupt(30);
    gic_enable_timer_irq();

    uart_puts("Initializing task scheduler...\n");
    scheduler_init();
    scheduler_enabled = 1;

    uart_puts("Enabling interrupts...\n");
    asm volatile("msr daifclr, #2");

    uart_puts("\nSystem ready!\n");
    uart_puts("Type 'help' for available commands.\n\n");

    char input_buffer[128];
    while (1) {
        uart_puts("rpi4> ");
        uart_gets(input_buffer, sizeof(input_buffer));
        process_command(input_buffer);
    }
}
