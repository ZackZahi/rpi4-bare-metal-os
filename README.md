# Raspberry Pi 4 Bare Metal OS

A bare-metal operating system for Raspberry Pi 4, designed to run in QEMU.

## Features

- ✅ Multi-core boot (CPU 0 active, CPUs 1-3 halted)
- ✅ UART driver for serial I/O at 115200 baud
- ✅ GIC-400 interrupt controller driver
- ✅ ARM Generic Timer driver
- ✅ **Timer interrupts** (100ms interval)
- ✅ Exception handling infrastructure

## Prerequisites

Install the required tools on macOS:

```bash
brew install aarch64-elf-gcc
brew install qemu
```

## Project Structure

- `boot.S` - Assembly boot code (sets up stack, vector table, enables interrupts)
- `vectors.S` - Exception vector table and IRQ handler stub
- `kernel.c` - Main C kernel with UART driver and interrupt handling
- `timer.c` / `timer.h` - ARM Generic Timer driver with interrupt support
- `gic.c` / `gic.h` - GIC-400 interrupt controller driver
- `linker.ld` - Linker script defining memory layout
- `Makefile` - Build system

## Building

```bash
make
```

This produces `kernel8.img` which can be loaded by QEMU.

## Running

```bash
make run
```

You should see output like:
```
========================================
  Raspberry Pi 4 OS - Timer Interrupts
========================================

Initializing system...
Setting up GIC interrupt controller...
Timer frequency: 54000000 Hz
Setting up timer interrupts (100ms interval)...
System ready! Timer interrupts enabled.
You should see a tick message every second.

Timer tick: 10
Timer tick: 20
Timer tick: 30
...
```

Press `Ctrl+A` then `X` to exit QEMU.

## How It Works

### Boot Process

1. **boot.S** - CPU 0 initializes, sets up exception vector table at `vbar_el1`, enables IRQ interrupts
2. **kernel_main()** - Initializes UART, GIC, and timer, then enters WFI (Wait For Interrupt) loop
3. Timer generates interrupts every 100ms

### Interrupt Handling Flow

1. Hardware generates timer interrupt
2. GIC-400 signals the interrupt to CPU
3. CPU jumps to exception vector table (`vectors.S`)
4. **irq_handler_stub** saves all registers to stack
5. Calls C function **irq_handler()** in kernel.c
6. **irq_handler()** reads interrupt ID from GIC
7. Calls **timer_handle_irq()** to service the timer
8. Signals end-of-interrupt to GIC
9. Registers restored, execution returns via `eret`

### ARM Generic Timer

- Uses the built-in ARMv8 Generic Timer
- Configured for EL1 physical timer (CNTP)
- Frequency varies by QEMU version (typically 54 MHz)
- Generates periodic interrupts

### GIC-400 (Generic Interrupt Controller)

- Industry-standard ARM interrupt controller
- Distributor routes interrupts to CPUs
- CPU interface allows CPU to acknowledge and complete interrupts
- Timer interrupt ID: 30 (physical timer PPI)

## Raspberry Pi 4 Specific Details

- **CPU**: Quad-core Cortex-A72 (ARMv8-A, 64-bit)
- **Peripheral Base**: 0xFE000000
- **GIC Base**: 0xFF840000
- **UART Clock**: 48MHz
- **QEMU Support**: Uses `-M raspi4b` machine type

## Next Steps

Potential features to add:
- [ ] UART input (keyboard)
- [ ] Simple shell/command interface
- [ ] Memory management (MMU, page tables)
- [ ] Process scheduler
- [ ] Multi-core support (wake CPUs 1-3)
- [ ] GPIO control
- [ ] Framebuffer graphics

## Debugging

To debug with GDB:

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

## Technical Details

- **Architecture**: ARMv8-A (AArch64)
- **Exception Level**: EL1 (kernel mode)
- **Memory Layout**: Kernel at 0x80000, stack grows down from 0x80000
- **Interrupts**: IRQ enabled, FIQ/SError disabled
- **Timer**: ARM Generic Timer, physical timer (CNTP)
- **Interrupt Controller**: GIC-400
