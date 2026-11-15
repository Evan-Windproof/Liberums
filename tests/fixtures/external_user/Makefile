# SPDX-License-Identifier: GPL-3.0-or-later
# Independent fixture build: SDK headers only, no kernel object/library linkage.
CC = gcc
LD = ld
SDK ?= ../../../include
OUT ?= ../../../build/external
CFLAGS := -m32 -march=i686 -std=c11 -ffreestanding -fno-builtin -fno-pic -fno-pie \
          -fno-stack-protector -mno-sse -mno-mmx -msoft-float -g3 -O2 \
          -Wall -Wextra -Werror -Wpedantic -fno-asynchronous-unwind-tables
.PHONY: all clean
all: $(OUT)/normal.elf $(OUT)/fault.elf $(OUT)/runtime-abi.elf $(OUT)/minimal.elf $(OUT)/code-fault.elf
$(OUT):
	mkdir -p $@
$(OUT)/normal.o: program.c $(wildcard $(SDK)/abi/*.h) Makefile | $(OUT)
	$(CC) $(CFLAGS) -I$(SDK) -c $< -o $@
$(OUT)/fault.o: program.c $(wildcard $(SDK)/abi/*.h) Makefile | $(OUT)
	$(CC) $(CFLAGS) -I$(SDK) -DTEST_FAULT -c $< -o $@
$(OUT)/runtime-abi.o: program.c $(wildcard $(SDK)/abi/*.h) Makefile | $(OUT)
	$(CC) $(CFLAGS) -I$(SDK) -DTEST_RUNTIME_ABI -c $< -o $@
$(OUT)/minimal.o: minimal.c $(wildcard $(SDK)/abi/*.h) Makefile | $(OUT)
	$(CC) $(CFLAGS) -I$(SDK) -c $< -o $@
$(OUT)/code-fault.o: program.c $(wildcard $(SDK)/abi/*.h) Makefile | $(OUT)
	$(CC) $(CFLAGS) -I$(SDK) -DTEST_CODE_FAULT -c $< -o $@
$(OUT)/minimal.elf: $(OUT)/minimal.o linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T linker.ld $< -o $@
$(OUT)/padding.o: padding.S Makefile | $(OUT)
	$(CC) -m32 -g -c $< -o $@
$(OUT)/%.elf: $(OUT)/%.o $(OUT)/padding.o linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T linker.ld $(filter %.o,$^) -o $@
clean:
	rm -rf $(OUT)
$(OUT)/preempt.o: preempt.c $(wildcard $(SDK)/abi/*.h) Makefile | $(OUT)
	$(CC) $(CFLAGS) -I$(SDK) -c $< -o $@
all: $(OUT)/preempt.elf
