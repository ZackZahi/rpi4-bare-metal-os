# Makefile for Raspberry Pi 4 Bare Metal OS

# Toolchain
ARMGNU ?= aarch64-elf

# Compiler flags
CFLAGS = -Wall -O2 -ffreestanding -nostdinc -nostdlib -nostartfiles -mcpu=cortex-a72
ASMFLAGS = 

# Source files
OBJS = boot.o kernel.o

# Output
TARGET = kernel8.img
ELF = kernel8.elf

# Default target
all: $(TARGET)

# Compile C files
%.o: %.c
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

# Compile assembly files
%.o: %.S
	$(ARMGNU)-gcc $(ASMFLAGS) -c $< -o $@

# Link
$(ELF): $(OBJS) linker.ld
	$(ARMGNU)-ld -T linker.ld -o $(ELF) $(OBJS)

# Create raw binary
$(TARGET): $(ELF)
	$(ARMGNU)-objcopy $(ELF) -O binary $(TARGET)

# Run in QEMU
run: $(TARGET)
	qemu-system-aarch64 \
		-M raspi4b \
		-kernel $(TARGET) \
		-serial stdio

# Debug in QEMU (waits for GDB connection)
debug: $(TARGET)
	qemu-system-aarch64 \
		-M raspi4b \
		-kernel $(TARGET) \
		-serial stdio \
		-S -s

# Clean build files
clean:
	rm -f *.o *.elf *.img

# Phony targets
.PHONY: all clean run debug
