# SPDX-License-Identifier: GPL-3.0-or-later
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
SDK ?= ../../include
OUT ?= ../../build/external
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
FLAGS := -m32 -march=i686 -std=c11 -ffreestanding -fno-builtin -fno-pic -fno-pie \
         -fno-stack-protector -fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables \
         -mno-sse -mno-sse2 -mno-mmx -msoft-float -g3 -O2 -Wall -Wextra -Werror -Wpedantic
HEADERS := ../console/core.h $(wildcard $(SDK)/abi/*.h)
.PHONY: all
all: $(OUT)/console.elf $(OUT)/amber.elf
$(OUT):
	mkdir -p $@
$(OUT)/console-core.o: ../console/core.c $(HEADERS) Makefile | $(OUT)
	$(CC) -I$(SDK) $(FLAGS) -c $< -o $@
$(OUT)/console-main.o: main.c $(HEADERS) Makefile | $(OUT)
	$(CC) -I$(SDK) $(FLAGS) -c $< -o $@
$(OUT)/amber-main.o: main.c $(HEADERS) Makefile | $(OUT)
	$(CC) -I$(SDK) $(FLAGS) -DCONSOLE_ALTERNATE -c $< -o $@
$(OUT)/console-test-main.o: main.c $(HEADERS) Makefile | $(OUT)
	$(CC) -I$(SDK) $(FLAGS) -DCONSOLE_TEST -c $< -o $@
$(OUT)/console.elf: $(OUT)/console-main.o $(OUT)/console-core.o linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T linker.ld $(filter %.o,$^) -o $@
$(OUT)/amber.elf: $(OUT)/amber-main.o $(OUT)/console-core.o linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T linker.ld $(filter %.o,$^) -o $@
$(OUT)/console-test.elf: $(OUT)/console-test-main.o $(OUT)/console-core.o linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T linker.ld $(filter %.o,$^) -o $@

$(OUT)/probe.o: probe.c $(HEADERS) Makefile | $(OUT)
	$(CC) -I$(SDK) $(FLAGS) -c $< -o $@
$(OUT)/probe.elf: $(OUT)/probe.o linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T linker.ld $< -o $@
$(OUT)/layout.o: layout.c $(HEADERS) Makefile | $(OUT)
	$(CC) -I$(SDK) $(FLAGS) -c $< -o $@
$(OUT)/layout.elf: $(OUT)/layout.o layout.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T layout.ld $< -o $@
.PHONY: fixtures
fixtures: $(OUT)/probe.elf $(OUT)/layout.elf $(OUT)/console-test.elf
