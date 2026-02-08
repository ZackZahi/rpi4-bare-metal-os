# Minimal Raspberry Pi 4 OS for QEMU

A bare-metal operating system for Raspberry Pi 4, designed to run in QEMU.

## Prerequisites

Install the required tools on macOS:

```bash
brew install aarch64-elf-gcc
brew install qemu
```

## Project Structure

- `boot.S` - Assembly boot code (sets up stack, clears BSS, jumps to C)
- `kernel.c` - Main C kernel with UART driver
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
  Minimal Raspberry Pi 4 OS - QEMU
========================================

Hello from bare metal!
Kernel initialized successfully.

..........
```

Press `Ctrl+A` then `X` to exit QEMU.

## What This OS Does

1. **Boot**: CPU 0 starts, other cores are put to sleep
2. **Stack Setup**: Sets up stack pointer at 0x80000
3. **BSS Clearing**: Zeros out uninitialized data section
4. **UART Init**: Configures UART0 for 115200 baud serial output
5. **Hello World**: Prints messages via UART
6. **Idle Loop**: Prints dots periodically

## Raspberry Pi 4 Specific Details

- **CPU**: Quad-core Cortex-A72 (ARMv8-A, 64-bit)
- **Peripheral Base**: 0xFE000000 (different from Pi 3's 0x3F000000)
- **UART Clock**: 48MHz (different from Pi 3's 3MHz)
- **QEMU Support**: Uses `-M raspi4b` machine type

## Next Steps

Potential features to add:
- Exception/interrupt handlers
- Timer interrupts
- Keyboard input
- Memory management
- Simple scheduler
- GPIO control
- Framebuffer graphics

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

## Notes

- The kernel loads at physical address 0x80000
- UART is configured for 115200 baud, 8N1
- Stack grows downward from 0x80000
- Only CPU 0 is active; CPUs 1-3 are halted in WFE
