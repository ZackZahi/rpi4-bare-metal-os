# Raspberry Pi 4 Bare Metal OS

A bare-metal operating system for Raspberry Pi 4, designed to run in QEMU.

## Features

* ✅ Multi-core boot (CPU 0 active, CPUs 1-3 halted)
* ✅ EL2 → EL1 transition (QEMU raspi4b boots at EL2)
* ✅ **UART driver with input/output** at 115200 baud
* ✅ **Interactive command-line interface**
* ✅ GIC-400 interrupt controller driver
* ✅ ARM Local Peripherals timer IRQ routing (QEMU-compatible)
* ✅ ARM Generic Timer driver
* ✅ Timer interrupts (100ms interval)
* ✅ Exception handling infrastructure

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
│   ├── boot.S              - Boot code (EL2 → EL1 drop)
│   ├── vectors.S           - Exception vectors
│   ├── kernel.c            - Main kernel with command processor
│   └── drivers/
│       ├── uart.c          - UART driver (input/output)
│       ├── timer.c         - Timer driver
│       └── gic.c           - Interrupt controller (GIC + Local Peripherals)
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

```
make
```

This produces `kernel8.img` which can be loaded by QEMU.

## Running

```
make run
```

You should see:

```
========================================
  Raspberry Pi 4 OS
========================================

Initializing system...
Setting up GIC interrupt controller...
Timer frequency: 62500000 Hz
Setting up timer interrupts (100ms interval)...
System ready!

Type 'help' for available commands.

rpi4> _
```

Press `Ctrl+A` then `X` to exit QEMU.

## Available Commands

Type these at the `rpi4>` prompt:

* `help` - Show available commands
* `echo` - Echo back your input
* `time` - Show system uptime
* `info` - Display system information
* `clear` - Clear the screen
* `hello` - Print a greeting

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
Uptime: 8 seconds (80 ticks)

rpi4> info
Raspberry Pi 4 Bare Metal OS
CPU: ARM Cortex-A72 (ARMv8-A)
Timer frequency: 62500000 Hz
Features: UART I/O, Timer Interrupts, GIC-400
```

## How It Works

### Boot Process

1. **boot.S** - CPU 0 checks current EL; if EL2, configures HCR_EL2 for AArch64 at EL1, enables timer access, then drops to EL1 via `eret`
2. **boot.S** - At EL1: sets up stack, exception vector table (`vbar_el1`), clears BSS, enables IRQs
3. **kernel_main()** - Initializes UART, GIC, ARM Local Peripherals timer routing, and timer
4. Enters interactive command loop

### Timer Interrupts

QEMU's raspi4b routes the ARM Generic Timer interrupt through the BCM2836-style ARM Local Peripherals controller (at `0xFF800000`) rather than the GIC-400. The IRQ handler checks `CNTP_CTL_EL0.ISTATUS` directly to detect timer expiry, then re-arms the timer by writing `CNTP_TVAL_EL0`.

Timer interrupts fire every 100ms in the background while the command loop runs.

### UART Driver

* **Input**: Blocking (`uart_getc`) and non-blocking (`uart_getc_nonblock`) read
* **Output**: Character and string output with formatting
* **Line editing**: Backspace, Ctrl+C, Ctrl+U support
* **Echo**: Characters are echoed as you type

## Technical Details

* **Architecture**: ARMv8-A (AArch64)
* **CPU**: Cortex-A72
* **Execution Level**: EL1 (drops from EL2 at boot)
* **Peripheral Base**: 0xFE000000
* **ARM Local Peripherals**: 0xFF800000
* **GIC Base**: 0xFF840000
* **UART**: PL011, 115200 baud, 8N1
* **Timer**: ARM Generic Timer (CNTP), 62.5 MHz on QEMU

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
