# SPDX-License-Identifier: GPL-3.0-or-later
.DEFAULT_GOAL := all
OUT ?= ../../../build/phase9
SDK ?= ../../../include
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
FLAGS := -m32 -march=i686 -mno-mmx -mno-sse -mno-sse2 -msoft-float -std=c11 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables -g3 -O2 -Wall -Wextra -Werror -Wpedantic
all: $(OUT)/abi-probe.elf
$(OUT)/abi-probe.o: abi_probe.c $(wildcard $(SDK)/abi/*.h)
	mkdir -p $(OUT)
	$(CC) $(FLAGS) -I$(SDK) -c $< -o $@
$(OUT)/abi-probe.elf: $(OUT)/abi-probe.o ../domains8/linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T ../domains8/linker.ld $< -o $@
# Build a consumer against the independently exported SDK; never edit its files.
.PHONY: ael
AEL_SDK ?= /path/to/exported-sdk
APP_POLICY ?= first
ael:
	$(MAKE) -f "$(AEL_SDK)/app.mk" AEL_SDK="$(AEL_SDK)" OUT="$(OUT)/ael-$(APP_POLICY)" SOURCES=ael_ram.c APP_PROFILE=preempt APP_POLICY="$(APP_POLICY)"
