# Makefile for Raspberry Pi 4 Bare Metal OS

ARMGNU ?= aarch64-elf

SRC_DIR = src
DRIVER_DIR = $(SRC_DIR)/drivers
INC_DIR = include
BUILD_DIR = build

CFLAGS = -Wall -O2 -ffreestanding -nostdinc -nostdlib -nostartfiles -mcpu=cortex-a72 -I$(INC_DIR)
ASMFLAGS =

OBJS = $(BUILD_DIR)/boot.o \
       $(BUILD_DIR)/vectors.o \
       $(BUILD_DIR)/kernel.o \
       $(BUILD_DIR)/uart.o \
       $(BUILD_DIR)/timer.o \
       $(BUILD_DIR)/gic.o \
       $(BUILD_DIR)/task.o \
       $(BUILD_DIR)/memory.o \
       $(BUILD_DIR)/mmu.o \
       $(BUILD_DIR)/fs.o

TARGET = kernel8.img
ELF = kernel8.elf

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(DRIVER_DIR)/%.c | $(BUILD_DIR)
	$(ARMGNU)-gcc $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	$(ARMGNU)-gcc $(ASMFLAGS) -c $< -o $@

$(ELF): $(OBJS) linker.ld
	$(ARMGNU)-ld -T linker.ld -o $(ELF) $(OBJS)

$(TARGET): $(ELF)
	$(ARMGNU)-objcopy $(ELF) -O binary $(TARGET)

run: $(TARGET)
	qemu-system-aarch64 \
		-M raspi4b \
		-kernel $(TARGET) \
		-serial stdio

debug: $(TARGET)
	qemu-system-aarch64 \
		-M raspi4b \
		-kernel $(TARGET) \
		-serial stdio \
		-S -s

clean:
	rm -rf $(BUILD_DIR) *.elf *.img

.PHONY: all clean run debug
