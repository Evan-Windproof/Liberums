# SPDX-License-Identifier: GPL-3.0-or-later
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
SDK ?= ../../../include
OUT ?= ../../../build/phase8
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
FLAGS := -m32 -march=i686 -std=c11 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-sse -mno-sse2 -mno-mmx -msoft-float -g3 -O2 -Wall -Wextra -Werror -Wpedantic
HEADERS := $(wildcard $(SDK)/abi/*.h)
all: $(OUT)/worker-a.elf $(OUT)/worker-b.elf
$(OUT):
	mkdir -p $@
$(OUT)/worker-a.o: worker.c $(HEADERS) Makefile | $(OUT)
	$(CC) -I$(SDK) $(FLAGS) -DWORKER_MARKER=0xa8000000u -c $< -o $@
$(OUT)/worker-b.o: worker.c $(HEADERS) Makefile | $(OUT)
	$(CC) -I$(SDK) $(FLAGS) -DWORKER_MARKER=0xb8000000u -c $< -o $@
$(OUT)/spin.o: spin.S Makefile | $(OUT)
	$(CC) -m32 -g3 -c $< -o $@
$(OUT)/worker-%.elf: $(OUT)/worker-%.o $(OUT)/spin.o linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T linker.ld $(filter %.o,$^) -o $@
.SECONDARY:
