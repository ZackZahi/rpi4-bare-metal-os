// kernel.c - Raspberry Pi 4 Bare Metal OS
// Features: Enhanced shell with history, tab completion, kill, top

#include "uart.h"
#include "timer.h"
#include "gic.h"
#include "task.h"
#include "memory.h"
#include "mmu.h"
#include "fs.h"

static volatile int scheduler_enabled = 0;

// ========== IRQ Handler ==========

unsigned long irq_handler_c(unsigned long sp) {
    unsigned long ctl;
    asm volatile("mrs %0, cntp_ctl_el0" : "=r"(ctl));

    if (ctl & 0x4) {
        timer_handle_irq();

        if (scheduler_enabled) {
            sp = schedule_irq(sp);
        }
    }

    return sp;
}

// ========== String Utilities ==========

static int str_eq(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *s1 == *s2;
}

static int str_neq(const char *s1, const char *s2, int n) {
    while (n > 0 && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static int str_len(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static void str_cpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static unsigned long parse_num(const char *s) {
    unsigned long val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return val;
}

// ========== Shell: Command History ==========

#define HISTORY_SIZE 16
#define LINE_MAX     128

static char history[HISTORY_SIZE][LINE_MAX];
static int history_count = 0;    // Total commands stored
static int history_write = 0;    // Next write position (circular)

static void history_add(const char *cmd) {
    if (cmd[0] == '\0') return;  // Don't store empty commands

    // Don't store duplicate of last command
    if (history_count > 0) {
        int last = (history_write - 1 + HISTORY_SIZE) % HISTORY_SIZE;
        if (str_eq(history[last], cmd)) return;
    }

    str_cpy(history[history_write], cmd);
    history_write = (history_write + 1) % HISTORY_SIZE;
    if (history_count < HISTORY_SIZE)
        history_count++;
}

static const char *history_get(int index) {
    // index 0 = most recent, 1 = one before that, etc.
    if (index < 0 || index >= history_count) return 0;
    int pos = (history_write - 1 - index + HISTORY_SIZE * 2) % HISTORY_SIZE;
    return history[pos];
}

// ========== Shell: Tab Completion ==========

// Print the current shell prompt (used by tab complete, Ctrl+L, top)
static void print_prompt(void) {
    char path[FS_PATH_MAX];
    fs_get_path(fs_get_cwd(), path, FS_PATH_MAX);
    uart_puts("rpi4:");
    uart_puts(path);
    uart_puts("> ");
}

static const char *commands[] = {
    "help", "time", "info", "clear", "ps", "spawn", "memtest",
    "mem", "alloc", "pgalloc", "pgfree", "kill", "top", "history", "mmu",
    "ls", "cd", "pwd", "mkdir", "rmdir", "touch", "cat", "write", "rm",
    0
};

static void tab_complete(char *buf, int *pos) {
    if (*pos == 0) return;

    // Find all matching commands
    const char *match = 0;
    int match_count = 0;

    for (int i = 0; commands[i]; i++) {
        int ok = 1;
        for (int j = 0; j < *pos; j++) {
            if (commands[i][j] != buf[j]) { ok = 0; break; }
            if (commands[i][j] == '\0') { ok = 0; break; }
        }
        if (ok) {
            match = commands[i];
            match_count++;
        }
    }

    if (match_count == 1) {
        // Complete the command
        int mlen = str_len(match);
        for (int i = *pos; i < mlen && i < LINE_MAX - 2; i++) {
            buf[i] = match[i];
            uart_putc(match[i]);
            (*pos)++;
        }
        // Add a space after completion
        if (*pos < LINE_MAX - 1) {
            buf[*pos] = ' ';
            uart_putc(' ');
            (*pos)++;
        }
    } else if (match_count > 1) {
        // Show all matches
        uart_puts("\n");
        for (int i = 0; commands[i]; i++) {
            int ok = 1;
            for (int j = 0; j < *pos; j++) {
                if (commands[i][j] != buf[j]) { ok = 0; break; }
                if (commands[i][j] == '\0') { ok = 0; break; }
            }
            if (ok) {
                uart_puts("  ");
                uart_puts(commands[i]);
                uart_puts("\n");
            }
        }
        // Reprint prompt and current input
        print_prompt();
        for (int i = 0; i < *pos; i++)
            uart_putc(buf[i]);
    }
}

// ========== Shell: Line Editor ==========

static void shell_readline(char *buf) {
    int pos = 0;
    int hist_idx = -1;  // -1 = not browsing history
    char saved[LINE_MAX];  // Save current input when browsing history
    saved[0] = '\0';

    buf[0] = '\0';

    while (1) {
        unsigned char c = uart_getc();

        // ---- Enter ----
        if (c == '\r' || c == '\n') {
            buf[pos] = '\0';
            uart_puts("\n");
            history_add(buf);
            return;
        }

        // ---- Backspace ----
        if (c == 0x7F || c == 0x08) {
            if (pos > 0) {
                pos--;
                uart_puts("\b \b");
            }
            continue;
        }

        // ---- Ctrl+C ----
        if (c == 0x03) {
            uart_puts("^C\n");
            buf[0] = '\0';
            return;
        }

        // ---- Ctrl+U (clear line) ----
        if (c == 0x15) {
            while (pos > 0) {
                uart_puts("\b \b");
                pos--;
            }
            continue;
        }

        // ---- Ctrl+A (home) ----
        if (c == 0x01) {
            while (pos > 0) {
                uart_putc('\b');
                pos--;
            }
            continue;
        }

        // ---- Ctrl+E (end - already at end in this simple impl) ----
        if (c == 0x05) {
            continue;
        }

        // ---- Ctrl+L (clear screen, reprint prompt) ----
        if (c == 0x0C) {
            uart_puts("\033[2J\033[H");
            print_prompt();
            for (int i = 0; i < pos; i++)
                uart_putc(buf[i]);
            continue;
        }

        // ---- Tab ----
        if (c == '\t') {
            tab_complete(buf, &pos);
            continue;
        }

        // ---- Escape sequence (arrows) ----
        if (c == 0x1B) {
            // Read next two bytes of escape sequence
            unsigned char seq1 = uart_getc();
            if (seq1 != '[') continue;  // Not a CSI sequence
            unsigned char seq2 = uart_getc();

            if (seq2 == 'A') {
                // ---- Up arrow: previous command ----
                int new_idx = hist_idx + 1;
                const char *h = history_get(new_idx);
                if (h) {
                    // Save current input if first time browsing
                    if (hist_idx == -1) {
                        for (int i = 0; i < pos; i++) saved[i] = buf[i];
                        saved[pos] = '\0';
                    }
                    hist_idx = new_idx;
                    // Erase current line
                    while (pos > 0) { uart_puts("\b \b"); pos--; }
                    // Copy history entry
                    str_cpy(buf, h);
                    pos = str_len(buf);
                    for (int i = 0; i < pos; i++) uart_putc(buf[i]);
                }
            } else if (seq2 == 'B') {
                // ---- Down arrow: next command ----
                if (hist_idx > 0) {
                    hist_idx--;
                    const char *h = history_get(hist_idx);
                    while (pos > 0) { uart_puts("\b \b"); pos--; }
                    if (h) {
                        str_cpy(buf, h);
                        pos = str_len(buf);
                        for (int i = 0; i < pos; i++) uart_putc(buf[i]);
                    }
                } else if (hist_idx == 0) {
                    // Restore saved input
                    hist_idx = -1;
                    while (pos > 0) { uart_puts("\b \b"); pos--; }
                    str_cpy(buf, saved);
                    pos = str_len(buf);
                    for (int i = 0; i < pos; i++) uart_putc(buf[i]);
                }
            }
            // Ignore other escape sequences (left/right arrows, etc.)
            continue;
        }

        // ---- Printable characters ----
        if (c >= 32 && c < 127 && pos < LINE_MAX - 1) {
            buf[pos++] = c;
            uart_putc(c);
        }
    }
}

// ========== Demo Tasks ==========

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

        uart_puts("[memtest] Verifying: ");
        uart_putc(buf1[0]); uart_putc(buf2[0]); uart_putc(buf3[0]);
        uart_puts(" (expect ABC)\n");

        task_sleep(2000);

        kfree((void *)buf1);
        kfree((void *)buf2);
        kfree((void *)buf3);

        void *page = page_alloc();
        if (page) {
            volatile char *p = (volatile char *)page;
            for (int i = 0; i < 4096; i++) p[i] = 'X';
            uart_puts("[memtest] Page write OK\n");
            page_free(page);
        }
    } else {
        uart_puts("[memtest] Allocation failed!\n");
    }

    uart_puts("[memtest] Done. Free: ");
    uart_put_dec(memory_get_free_pages());
    uart_puts(" pages\n");
}

// ========== Command Processor ==========

static const char *state_name(task_state_t s) {
    switch (s) {
        case TASK_READY:   return "READY";
        case TASK_RUNNING: return "RUNNING";
        case TASK_BLOCKED: return "BLOCKED";
        case TASK_DEAD:    return "DEAD";
        default:           return "?";
    }
}

static void cmd_help(void) {
    uart_puts("Available commands:\n");
    uart_puts("  help          Show this help message\n");
    uart_puts("  time          Show current tick count\n");
    uart_puts("  info          Show system information\n");
    uart_puts("  clear         Clear screen\n");
    uart_puts("  ps            List all tasks\n");
    uart_puts("  spawn         Launch demo tasks (counter + spinner)\n");
    uart_puts("  kill ID       Terminate a task by ID\n");
    uart_puts("  top           Live task monitor (any key to exit)\n");
    uart_puts("  memtest       Launch memory test task\n");
    uart_puts("  mem           Show memory statistics\n");
    uart_puts("  alloc N       Allocate N bytes\n");
    uart_puts("  pgalloc       Allocate a 4KB page\n");
    uart_puts("  pgfree A      Free page at hex address A\n");
    uart_puts("  mmu           Show MMU/cache configuration\n");
    uart_puts("  history       Show command history\n");
    uart_puts("\nFilesystem:\n");
    uart_puts("  ls [path]     List directory contents\n");
    uart_puts("  cd [path]     Change directory (cd .. to go up)\n");
    uart_puts("  pwd           Print working directory\n");
    uart_puts("  mkdir PATH    Create directory\n");
    uart_puts("  rmdir PATH    Remove empty directory\n");
    uart_puts("  touch PATH    Create empty file\n");
    uart_puts("  cat PATH      Show file contents\n");
    uart_puts("  write PATH    Write text to file (interactive)\n");
    uart_puts("  rm PATH       Remove file\n");
    uart_puts("\nShell features:\n");
    uart_puts("  Up/Down       Browse command history\n");
    uart_puts("  Tab           Auto-complete commands\n");
    uart_puts("  Ctrl+C        Cancel current input\n");
    uart_puts("  Ctrl+U        Clear current line\n");
    uart_puts("  Ctrl+L        Clear screen\n");
}

static void cmd_ps(void) {
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
        int len = str_len(pool[i].name);
        for (int j = len; j < 16; j++) uart_putc(' ');
        uart_puts(state_name(pool[i].state));
        if (&pool[i] == get_current_task()) uart_puts(" <-- current");
        uart_puts("\n");
    }
}

static void cmd_top(void) {
    uart_puts("Live task monitor (press any key to exit)\n\n");

    while (1) {
        // Check for keypress to exit
        int c = uart_getc_nonblock();
        if (c >= 0) break;

        // Move cursor to top-left of task area
        uart_puts("\033[3;1H");  // Row 3 (after header lines)
        uart_puts("\033[J");     // Clear from cursor to end of screen

        task_t *pool = get_task_pool();
        uart_puts("ID  NAME            STATE       TICKS\n");
        uart_puts("--  ----            -----       -----\n");

        int active = 0;
        for (int i = 0; i < MAX_TASKS; i++) {
            if (pool[i].state == TASK_DEAD && pool[i].name[0] == '\0') continue;
            if (pool[i].state == TASK_DEAD && i != 0) continue;

            uart_put_dec(pool[i].id);
            if (pool[i].id < 10) uart_puts("   ");
            else uart_puts("  ");
            uart_puts(pool[i].name);
            int len = str_len(pool[i].name);
            for (int j = len; j < 16; j++) uart_putc(' ');
            uart_puts(state_name(pool[i].state));

            // Pad state column
            int slen = str_len(state_name(pool[i].state));
            for (int j = slen; j < 12; j++) uart_putc(' ');

            if (pool[i].state == TASK_BLOCKED) {
                long remaining = (long)pool[i].sleep_until - (long)timer_get_tick_count();
                if (remaining > 0) {
                    uart_put_dec((unsigned long)remaining);
                    uart_puts(" left");
                }
            }

            if (&pool[i] == get_current_task()) uart_puts(" *");
            uart_puts("\n");
            active++;
        }

        uart_puts("\nUptime: ");
        uart_put_dec(timer_get_tick_count() / 10);
        uart_puts("s  Tasks: ");
        uart_put_dec(active);
        uart_puts("/");
        uart_put_dec(MAX_TASKS);
        uart_puts("  Free mem: ");
        uart_put_dec(memory_get_free_pages());
        uart_puts(" pages\n");

        // Wait ~500ms before refresh
        // (Use a simple polling delay since we don't want task_sleep in shell)
        unsigned long start;
        asm volatile("mrs %0, cntpct_el0" : "=r"(start));
        unsigned long freq;
        asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
        unsigned long target = start + freq / 2;  // 500ms
        while (1) {
            unsigned long now;
            asm volatile("mrs %0, cntpct_el0" : "=r"(now));
            if (now >= target) break;
            // Check for keypress during wait
            int k = uart_getc_nonblock();
            if (k >= 0) goto top_done;
        }
    }

top_done:
    uart_puts("\033[2J\033[H");  // Clear screen
}

static void cmd_kill(const char *arg) {
    while (*arg == ' ') arg++;
    if (*arg == '\0') {
        uart_puts("Usage: kill <task_id>\n");
        return;
    }
    unsigned long id = parse_num(arg);
    if (id == 0) {
        uart_puts("Cannot kill the shell (task 0)\n");
        return;
    }

    // Find the task name before killing
    task_t *pool = get_task_pool();
    const char *name = 0;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (pool[i].id == (unsigned int)id && pool[i].state != TASK_DEAD) {
            name = pool[i].name;
            break;
        }
    }

    if (task_kill((unsigned int)id) == 0) {
        uart_puts("Killed task ");
        uart_put_dec(id);
        if (name) {
            uart_puts(" (");
            uart_puts(name);
            uart_puts(")");
        }
        uart_puts("\n");
    } else {
        uart_puts("Task ");
        uart_put_dec(id);
        uart_puts(" not found or cannot be killed\n");
    }
}

static void cmd_history_show(void) {
    if (history_count == 0) {
        uart_puts("No command history\n");
        return;
    }
    for (int i = history_count - 1; i >= 0; i--) {
        const char *h = history_get(i);
        if (h) {
            uart_put_dec(history_count - i);
            uart_puts("  ");
            uart_puts(h);
            uart_puts("\n");
        }
    }
}

// Skip leading spaces and return pointer to argument
static const char *skip_arg(const char *cmd, int cmdlen) {
    const char *p = cmd + cmdlen;
    while (*p == ' ') p++;
    return p;
}

static void cmd_write_interactive(const char *path) {
    if (!path || path[0] == '\0') {
        uart_puts("Usage: write <filename>\n");
        return;
    }

    // Create file if needed
    fs_node_t *file = fs_resolve(path);
    if (file && file->type == FS_DIR) {
        uart_puts("write: is a directory\n");
        return;
    }

    uart_puts("Enter text (Ctrl+D on empty line to finish):\n");

    // Read lines into a buffer
    char content[FS_MAX_DATA];
    int total = 0;

    while (total < FS_MAX_DATA - 2) {
        uart_puts("> ");
        // Read one line
        char line[256];
        int lpos = 0;
        while (1) {
            unsigned char c = uart_getc();
            if (c == 0x04) {  // Ctrl+D
                if (lpos == 0) {
                    uart_puts("\n");
                    goto done_writing;
                }
                continue;
            }
            if (c == '\r' || c == '\n') {
                line[lpos] = '\0';
                uart_puts("\n");
                break;
            }
            if ((c == 0x7F || c == 0x08) && lpos > 0) {
                lpos--;
                uart_puts("\b \b");
                continue;
            }
            if (c == 0x03) {  // Ctrl+C — abort
                uart_puts("^C\n");
                uart_puts("write: aborted\n");
                return;
            }
            if (c >= 32 && c < 127 && lpos < 255) {
                line[lpos++] = c;
                uart_putc(c);
            }
        }

        // Append line + newline to content
        for (int i = 0; line[i] && total < FS_MAX_DATA - 2; i++)
            content[total++] = line[i];
        if (total < FS_MAX_DATA - 2)
            content[total++] = '\n';
    }

done_writing:
    content[total] = '\0';

    if (total > 0) {
        fs_write(path, content);
        uart_puts("Wrote ");
        uart_put_dec(total);
        uart_puts(" bytes to ");
        uart_puts(path);
        uart_puts("\n");
    } else {
        uart_puts("write: nothing written\n");
    }
}

static void process_command(char *cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    if (str_eq(cmd, "help"))    { cmd_help(); return; }
    if (str_eq(cmd, "clear"))   { uart_puts("\033[2J\033[H"); return; }
    if (str_eq(cmd, "ps"))      { cmd_ps(); return; }
    if (str_eq(cmd, "top"))     { cmd_top(); return; }
    if (str_eq(cmd, "history")) { cmd_history_show(); return; }
    if (str_eq(cmd, "mmu"))     { mmu_dump_config(); return; }

    if (str_eq(cmd, "time")) {
        unsigned long ticks = timer_get_tick_count();
        uart_puts("Uptime: ");
        uart_put_dec(ticks / 10);
        uart_puts(" seconds (");
        uart_put_dec(ticks);
        uart_puts(" ticks)\n");
        return;
    }

    if (str_eq(cmd, "info")) {
        uart_puts("Raspberry Pi 4 Bare Metal OS\n");
        uart_puts("CPU: ARM Cortex-A72 (ARMv8-A)\n");
        uart_puts("Timer: ");
        uart_put_dec(timer_get_frequency());
        uart_puts(" Hz\n");
        uart_puts("Scheduler: preemptive round-robin (100ms quantum)\n");
        uart_puts("Max tasks: ");
        uart_put_dec(MAX_TASKS);
        uart_puts("\n");
        uart_puts("Memory: ");
        uart_put_dec((memory_get_free_pages() * PAGE_SIZE) / (1024 * 1024));
        uart_puts(" MB free / ");
        uart_put_dec((memory_get_total_pages() * PAGE_SIZE) / (1024 * 1024));
        uart_puts(" MB total\n");
        return;
    }

    if (str_eq(cmd, "spawn")) {
        uart_puts("Spawning 'counter' and 'spinner'...\n");
        task_create(task_counter, "counter");
        task_create(task_spinner, "spinner");
        return;
    }

    if (str_eq(cmd, "memtest")) {
        uart_puts("Spawning 'memtest'...\n");
        task_create(task_memtest, "memtest");
        return;
    }

    if (str_eq(cmd, "mem")) {
        uart_puts("Total: ");
        uart_put_dec(memory_get_total_pages());
        uart_puts(" pages (");
        uart_put_dec((memory_get_total_pages() * PAGE_SIZE) / (1024 * 1024));
        uart_puts(" MB)  Used: ");
        uart_put_dec(memory_get_used_pages());
        uart_puts("  Free: ");
        uart_put_dec(memory_get_free_pages());
        uart_puts("\n");
        return;
    }

    if (str_neq(cmd, "alloc ", 6) == 0) {
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

    if (str_eq(cmd, "pgalloc")) {
        void *page = page_alloc();
        if (page) {
            uart_puts("Page at ");
            uart_put_hex((unsigned long)page);
            uart_puts("\n");
        } else {
            uart_puts("Page allocation failed!\n");
        }
        return;
    }

    if (str_neq(cmd, "pgfree ", 7) == 0) {
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

    if (str_neq(cmd, "kill ", 5) == 0) {
        cmd_kill(cmd + 5);
        return;
    }

    // ---- Filesystem commands ----

    if (str_eq(cmd, "ls")) {
        fs_ls(0);
        return;
    }
    if (str_neq(cmd, "ls ", 3) == 0) {
        fs_ls(skip_arg(cmd, 2));
        return;
    }

    if (str_eq(cmd, "pwd")) {
        char path[FS_PATH_MAX];
        fs_get_path(fs_get_cwd(), path, FS_PATH_MAX);
        uart_puts(path);
        uart_puts("\n");
        return;
    }

    if (str_eq(cmd, "cd")) {
        fs_set_cwd(fs_get_root());
        return;
    }
    if (str_neq(cmd, "cd ", 3) == 0) {
        const char *arg = skip_arg(cmd, 2);
        if (arg[0] == '\0') {
            fs_set_cwd(fs_get_root());
        } else {
            fs_node_t *dir = fs_resolve(arg);
            if (!dir) {
                uart_puts("cd: not found: ");
                uart_puts(arg);
                uart_puts("\n");
            } else if (dir->type != FS_DIR) {
                uart_puts("cd: not a directory: ");
                uart_puts(arg);
                uart_puts("\n");
            } else {
                fs_set_cwd(dir);
            }
        }
        return;
    }

    if (str_neq(cmd, "mkdir ", 6) == 0) {
        const char *arg = skip_arg(cmd, 5);
        if (arg[0] == '\0') uart_puts("Usage: mkdir <dirname>\n");
        else fs_mkdir(arg);
        return;
    }

    if (str_neq(cmd, "rmdir ", 6) == 0) {
        const char *arg = skip_arg(cmd, 5);
        if (arg[0] == '\0') uart_puts("Usage: rmdir <dirname>\n");
        else fs_rmdir(arg);
        return;
    }

    if (str_neq(cmd, "touch ", 6) == 0) {
        const char *arg = skip_arg(cmd, 5);
        if (arg[0] == '\0') uart_puts("Usage: touch <filename>\n");
        else fs_touch(arg);
        return;
    }

    if (str_neq(cmd, "cat ", 4) == 0) {
        const char *arg = skip_arg(cmd, 3);
        if (arg[0] == '\0') {
            uart_puts("Usage: cat <filename>\n");
            return;
        }
        unsigned long size = 0;
        const char *data = fs_read(arg, &size);
        if (!data) {
            fs_node_t *node = fs_resolve(arg);
            if (node && node->type == FS_DIR) {
                uart_puts("cat: is a directory\n");
            } else if (!node) {
                uart_puts("cat: not found: ");
                uart_puts(arg);
                uart_puts("\n");
            } else {
                uart_puts("(empty)\n");
            }
        } else {
            uart_puts(data);
            // Add newline if content doesn't end with one
            if (size > 0 && data[size - 1] != '\n')
                uart_puts("\n");
        }
        return;
    }

    if (str_neq(cmd, "write ", 6) == 0) {
        cmd_write_interactive(skip_arg(cmd, 5));
        return;
    }

    if (str_neq(cmd, "rm ", 3) == 0) {
        const char *arg = skip_arg(cmd, 2);
        if (arg[0] == '\0') uart_puts("Usage: rm <filename>\n");
        else fs_rm(arg);
        return;
    }

    uart_puts("Unknown: ");
    uart_puts(cmd);
    uart_puts("  (try 'help')\n");
}

// ========== Kernel Entry Point ==========

void kernel_main(void) {
    uart_init();

    uart_puts("\033[2J\033[H");
    uart_puts("\n");
    uart_puts("========================================\n");
    uart_puts("  Raspberry Pi 4 OS\n");
    uart_puts("========================================\n\n");

    uart_puts("Initializing memory...\n");
    memory_init();

    uart_puts("Initializing MMU...\n");
    mmu_init();

    uart_puts("Initializing filesystem...\n");
    fs_init();

    uart_puts("Setting up GIC...\n");
    gic_init();

    uart_puts("Timer: ");
    uart_put_dec(timer_get_frequency());
    uart_puts(" Hz\n");

    timer_init(100);
    gic_enable_interrupt(30);
    gic_enable_timer_irq();

    uart_puts("Scheduler init...\n");
    scheduler_init();
    scheduler_enabled = 1;

    uart_puts("Enabling IRQs...\n");
    asm volatile("msr daifclr, #2");

    uart_puts("\nReady! Type 'help' for commands.\n");
    uart_puts("Use Tab to complete, Up/Down for history.\n\n");

    char input_buffer[LINE_MAX];
    while (1) {
        print_prompt();
        shell_readline(input_buffer);
        process_command(input_buffer);
    }
}
