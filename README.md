# Raspberry Pi 4 Bare Metal OS

A bare-metal operating system for Raspberry Pi 4, developed and tested in QEMU.

## Features

* ✅ **Multi-core SMP** — all 4 Cortex-A72 cores active with per-core timers and spinlocks
* ✅ **Preemptive scheduler** — round-robin with 100ms quantum, trapframe-based context switching
* ✅ **Virtual memory (MMU)** — identity-mapped page tables, D-cache + I-cache enabled
* ✅ **Physical memory allocator** — 64MB managed, 2KB bitmap, kmalloc/kfree (256KB heap)
* ✅ **In-memory filesystem** — tree-structured ramfs with directories and files
* ✅ **Interactive shell** — command history, tab completion, line editing
* ✅ **UART driver** — PL011 at 115200 baud with blocking/non-blocking I/O
* ✅ **GIC-400 + ARM Local Peripherals** — per-core interrupt routing
* ✅ **ARM Generic Timer** — 100ms tick, SMP-safe
* ✅ **EL2 → EL1 transition** — for both primary and secondary cores

## Prerequisites

Install the required tools on macOS:

```
brew install aarch64-elf-gcc
brew install qemu
```

## Project Structure

```
rpi4-bare-metal-os/
├── src/
│   ├── boot.S              - Boot code (EL2 → EL1 drop, core 0 only)
│   ├── vectors.S           - Exception vectors (IRQ trapframe save/restore)
│   ├── context.S           - Context switch (trapframe-based)
│   ├── smp_entry.S         - Secondary core trampoline (EL2→EL1, MMU, stack)
│   ├── kernel.c            - Kernel main, IRQ handler, shell + commands
│   └── drivers/
│       ├── uart.c          - UART driver (input/output)
│       ├── timer.c         - ARM Generic Timer (SMP-safe)
│       ├── gic.c           - GIC-400 + per-core ARM Local Peripherals
│       ├── task.c          - Task scheduler (preemptive round-robin)
│       ├── memory.c        - Page allocator + kmalloc heap
│       ├── mmu.c           - MMU with identity-mapped page tables
│       ├── fs.c            - In-memory filesystem (ramfs)
│       └── smp.c           - Multi-core support (spinlocks, core wake)
├── include/
│   ├── uart.h
│   ├── timer.h
│   ├── gic.h
│   ├── task.h
│   ├── memory.h
│   ├── mmu.h
│   ├── fs.h
│   └── smp.h
├── build/                  - Build artifacts
├── linker.ld
├── Makefile
└── README.md
```

## Building

```
make
```

This produces `kernel8.img` which can be loaded by QEMU.

## Running

```
make run
```

This launches QEMU with 4 cores (`-smp 4`). You should see:

```
========================================
  Raspberry Pi 4 OS
========================================

Initializing memory...
Initializing MMU...
  MMU enabled! Identity-mapped with caches on.
Initializing filesystem...
Setting up GIC...
Timer: 62500000 Hz
Scheduler init...
Waking secondary cores...
  4/4 cores online
Enabling IRQs...

Ready! Type 'help' for commands.

rpi4:/> _
```

Press `Ctrl+A` then `X` to exit QEMU.

## Shell Commands

The prompt shows the current directory (`rpi4:/path>`).

### System

| Command | Description |
|---------|-------------|
| `help` | Show all commands |
| `info` | System info (CPU, cores, memory) |
| `time` | Show uptime |
| `clear` | Clear screen |
| `cpus` | Per-core status and tick counts |
| `mmu` | MMU/cache register dump |
| `mem` | Memory statistics |
| `history` | Command history |

### Tasks

| Command | Description |
|---------|-------------|
| `ps` | List all tasks |
| `spawn` | Launch demo tasks (counter + spinner) |
| `kill ID` | Terminate a task by ID |
| `top` | Live task monitor (any key to exit) |
| `memtest` | Launch memory stress test |

### Filesystem

| Command | Description |
|---------|-------------|
| `ls [path]` | List directory contents |
| `cd [path]` | Change directory |
| `pwd` | Print working directory |
| `mkdir PATH` | Create directory |
| `rmdir PATH` | Remove empty directory |
| `touch PATH` | Create empty file |
| `cat PATH` | Show file contents |
| `write PATH` | Write text interactively (Ctrl+D to finish) |
| `rm PATH` | Remove file |

### Memory

| Command | Description |
|---------|-------------|
| `alloc N` | Allocate N bytes from heap |
| `pgalloc` | Allocate a 4KB page |
| `pgfree ADDR` | Free page at hex address |

### Shell Features

* **Up/Down arrows** — browse command history (16 entries)
* **Tab** — auto-complete commands
* **Ctrl+C** — cancel input
* **Ctrl+U** — clear line
* **Ctrl+A** — jump to start of line
* **Ctrl+L** — clear screen

### Example Session

```
rpi4:/> info
Raspberry Pi 4 Bare Metal OS
CPU: ARM Cortex-A72 (ARMv8-A) x 4 cores
Timer: 62500000 Hz
Scheduler: preemptive round-robin (100ms quantum)
Max tasks: 8
Memory: 62 MB free / 64 MB total

rpi4:/> cpus
CORE  STATUS   TICKS
----  ------   -----
  0    online   412  <-- you
  1    online   410
  2    online   411
  3    online   410

rpi4:/> mkdir docs
rpi4:/> cd docs
rpi4:/docs> write hello.txt
Enter text (Ctrl+D on empty line to finish):
> Hello from bare metal!
>
Wrote 23 bytes to hello.txt
rpi4:/docs> cat hello.txt
Hello from bare metal!

rpi4:/docs> spawn
Spawning 'counter' and 'spinner'...
rpi4:/docs> ps
ID  NAME            STATE
--  ----            -----
0   shell           RUNNING <-- current
1   counter         BLOCKED
2   spinner         BLOCKED
```

## How It Works

### Boot Process

1. **boot.S** — Core 0 drops from EL2 to EL1, sets up stack, exception vectors, clears BSS
2. **kernel_main()** — Initializes memory allocator, MMU, filesystem, GIC, timer, scheduler
3. **smp_init()** — Writes entry point to spin table addresses (0xE0/0xE8/0xF0), sends SEV
4. **smp_entry.S** — Secondary cores drop EL2→EL1, enable MMU with shared page tables, set per-core stacks
5. **secondary_core_main()** — Each core configures its timer and enters a polling loop

### Multi-Core Architecture

All 4 Cortex-A72 cores are active. Core 0 runs the shell and handles IRQ-driven preemptive scheduling. Cores 1-3 run independent timer polling loops. Shared data is protected by ARMv8 spinlocks (LDAXR/STLXR with WFE/SEV).

QEMU's raspi4b only delivers timer IRQs to core 0 via the ARM Local Peripherals. Secondary cores poll the timer's ISTATUS bit instead — functionally equivalent.

### Memory Layout

| Address Range | Size | Description |
|---------------|------|-------------|
| `0x00000000 - 0x0007FFFF` | 512KB | Firmware / spin table |
| `0x00080000 - 0x0009FFFF` | ~128KB | Kernel code + data + BSS |
| `0x000A0000+` | ~64MB | Page allocator + heap |
| `0xC0000000 - 0xFFFFFFFF` | 1GB | Device memory (MMIO) |
| `0xFE000000` | — | BCM2711 peripherals (UART, GPIO) |
| `0xFF800000` | — | ARM Local Peripherals (timer routing) |
| `0xFF840000` | — | GIC-400 (distributor + CPU interface) |

### MMU Configuration

* 4KB granule, 48-bit virtual address space
* 2MB block descriptors at L2 (4-level page tables, 16KB total)
* RAM: Normal memory, write-back cacheable, inner shareable
* Devices: Device-nGnRnE, outer shareable
* Identity mapped (VA == PA)

## Technical Details

* **Architecture**: ARMv8-A (AArch64)
* **CPU**: Cortex-A72 × 4 cores
* **Execution Level**: EL1 (drops from EL2 at boot)
* **UART**: PL011, 115200 baud, 8N1
* **Timer**: ARM Generic Timer (CNTP), 62.5 MHz
* **Scheduler**: Preemptive round-robin, 100ms quantum, max 8 tasks
* **Memory**: 64MB managed, 4KB pages, 256KB kmalloc heap
* **Filesystem**: In-memory ramfs, 64 nodes, 4KB max file size

## Debugging

```
make debug
```

In another terminal:

```
aarch64-elf-gdb kernel8.elf
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```
