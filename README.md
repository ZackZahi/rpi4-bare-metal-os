# Raspberry Pi 4 Bare Metal OS

A bare-metal operating system for Raspberry Pi 4, designed to run in QEMU.

## Features

- ✅ Multi-core boot (CPU 0 active, CPUs 1-3 halted)
- ✅ **UART driver with input/output** at 115200 baud
- ✅ **Interactive command-line interface**
- ✅ GIC-400 interrupt controller driver
- ✅ ARM Generic Timer driver
- ✅ Timer interrupts (100ms interval)
- ✅ Exception handling infrastructure

## Prerequisites

Install the required tools on macOS:

```bash
brew install aarch64-elf-gcc
brew install qemu
```

## Project Structure

```
qemu-rpi4-kernel/
├── src/
│   ├── boot.S              - Boot code
│   ├── vectors.S           - Exception vectors
│   ├── kernel.c            - Main kernel with command processor
│   └── drivers/
│       ├── uart.c          - UART driver (input/output)
│       ├── timer.c         - Timer driver
│       └── gic.c           - Interrupt controller
├── include/
│   ├── uart.h
│   ├── timer.h
│   └── gic.h
├── build/                  - Build artifacts
├── linker.ld
├── Makefile
└── README.md
```

## Building

```bash
make
```

This produces `kernel8.img` which can be loaded by QEMU.

## Running

```bash
make run
```

You should see:
```
========================================
  Raspberry Pi 4 OS - UART Input
========================================

Initializing system...
Setting up GIC interrupt controller...
Timer frequency: 54000000 Hz
Setting up timer interrupts (100ms interval)...
System ready!

Type 'help' for available commands.

rpi4> _
```

Press `Ctrl+A` then `X` to exit QEMU.

## Available Commands

Type these at the `rpi4>` prompt:

- `help` - Show available commands
- `echo` - Echo back your input
- `time` - Show system uptime
- `info` - Display system information
- `clear` - Clear the screen
- `hello` - Print a greeting

### Example Session

```
rpi4> help
Available commands:
  help      - Show this help message
  echo      - Echo back what you type
  time      - Show current tick count
  info      - Show system information
  clear     - Clear screen
  hello     - Print a greeting

rpi4> hello
Hello from bare metal!
Welcome to Raspberry Pi 4 OS

rpi4> time
Uptime: 5 seconds (50 ticks)

rpi4> info
Raspberry Pi 4 Bare Metal OS
CPU: ARM Cortex-A72 (ARMv8-A)
Timer frequency: 54000000 Hz
Features: UART I/O, Timer Interrupts, GIC-400
```

## Features Explained

### UART Driver

- **Input**: Blocking (`uart_getc`) and non-blocking (`uart_getc_nonblock`) read
- **Output**: Character and string output with formatting
- **Line editing**: Backspace, Ctrl+C, Ctrl+U support
- **Echo**: Characters are echoed as you type

### Command Processing

- Simple command-line interface with prompt
- String parsing and command dispatch
- Easily extensible - add new commands in `process_command()`

### Timer Interrupts

- Background timer prints uptime every 10 seconds
- Runs independently while you type commands
- Demonstrates interrupt-driven multitasking

## How It Works

### Boot Process

1. **boot.S** - Initialize CPU, set up interrupts
2. **kernel_main()** - Initialize UART, GIC, timer
3. Enter command loop reading from UART

### Command Loop

1. Print prompt (`rpi4> `)
2. Read line with `uart_gets()` (blocks until Enter)
3. Parse and execute command
4. Repeat

### Interrupt Handling

- Timer interrupts fire every 100ms in background
- IRQ handler processes interrupt while command loop runs
- Demonstrates cooperative multitasking

## Technical Details

- **Architecture**: ARMv8-A (AArch64)
- **CPU**: Cortex-A72
- **Peripheral Base**: 0xFE000000
- **GIC Base**: 0xFF840000
- **UART**: PL011, 115200 baud, 8N1
- **Timer**: ARM Generic Timer (CNTP)

## Next Steps

- [ ] Command history (up/down arrows)
- [ ] Tab completion
- [ ] Memory management commands
- [ ] Process/task management
- [ ] File system support
- [ ] Multi-core commands

## Debugging

```bash
make debug
```

In another terminal:
```bash
aarch64-elf-gdb kernel8.elf
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```
