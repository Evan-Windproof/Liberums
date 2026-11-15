# SPDX-License-Identifier: GPL-3.0-or-later
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
QEMU ?= qemu-system-i386
GDB ?= gdb
PYTHON ?= python3
GDB_PORT ?= 1234

BUILD := build
KERNEL := $(BUILD)/liberums.elf
OBJECTS := $(BUILD)/boot.o $(BUILD)/kernel.o $(BUILD)/serial.o
ARCH_FLAGS := -m32 -march=i686 -mno-mmx -mno-sse -mno-sse2 -msoft-float
KERNEL_CFLAGS := $(ARCH_FLAGS) -std=c11 -ffreestanding -fno-builtin \
                 -fno-pic -fno-pie -fno-stack-protector -fno-common \
                 -fno-asynchronous-unwind-tables -fno-unwind-tables \
                 -fno-omit-frame-pointer -Wall -Wextra -Werror -Wpedantic -g3 -O2
QEMU_FLAGS := -machine pc -accel tcg -cpu qemu32 -m 32M -smp 1 \
              -display none -serial stdio -monitor none -nic none \
              -no-reboot -no-shutdown

.PHONY: all run debug test clean
all: $(KERNEL)

$(BUILD):
	mkdir -p $@

$(BUILD)/boot.o: arch/x86/boot.S Makefile | $(BUILD)
	$(CC) $(ARCH_FLAGS) -ffreestanding -fno-pic -fno-pie -g3 -c $< -o $@

$(BUILD)/kernel.o: kernel/main.c Makefile | $(BUILD)
	$(CC) -Iinclude $(CPPFLAGS) $(KERNEL_CFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/serial.o: kernel/serial.c Makefile | $(BUILD)
	$(CC) -Iinclude $(CPPFLAGS) $(KERNEL_CFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(KERNEL): $(OBJECTS) arch/x86/linker.ld Makefile
	$(LD) -m elf_i386 -nostdlib --build-id=none -T arch/x86/linker.ld \
		-Map=$(BUILD)/liberums.map $(LDFLAGS) $(OBJECTS) -o $@

run: $(KERNEL)
	$(QEMU) $(QEMU_FLAGS) -kernel $(KERNEL)

debug: $(KERNEL)
	$(QEMU) $(QEMU_FLAGS) -kernel $(KERNEL) -S -gdb tcp:127.0.0.1:$(GDB_PORT)

test: $(KERNEL)
	$(PYTHON) tests/phase1.py --qemu "$(QEMU)" --gdb "$(GDB)"

clean:
	rm -rf $(BUILD)

-include $(BUILD)/kernel.d $(BUILD)/serial.d
