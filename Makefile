# Makefile for Raspberry Pi 4 Bare Metal OS

# Toolchain
ARMGNU ?= aarch64-elf

# Directories
SRC_DIR = src
DRIVER_DIR = $(SRC_DIR)/drivers
INC_DIR = include
BUILD_DIR = build

# Compiler flags
CFLAGS = -Wall -O2 -ffreestanding -nostdinc -nostdlib -nostartfiles -mcpu=cortex-a72 -I$(INC_DIR)
ASMFLAGS = 

# Source files
C_SOURCES = $(SRC_DIR)/kernel.c $(DRIVER_DIR)/timer.c $(DRIVER_DIR)/gic.c
ASM_SOURCES = $(SRC_DIR)/boot.S $(SRC_DIR)/vectors.S

# Object files
OBJS = $(BUILD_DIR)/boot.o $(BUILD_DIR)/vectors.o $(BUILD_DIR)/kernel.o \
       $(BUILD_DIR)/timer.o $(BUILD_DIR)/gic.o

# Output
TARGET = kernel8.img
ELF = kernel8.elf

# Default target
all: $(BUILD_DIR) $(TARGET)

# Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Compile C files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(DRIVER_DIR)/%.c | $(BUILD_DIR)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

# Compile assembly files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
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
	rm -rf $(BUILD_DIR) *.elf *.img

# Phony targets
.PHONY: all clean run debug
