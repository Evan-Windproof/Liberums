# SPDX-License-Identifier: GPL-3.0-or-later
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
OUT ?= build
SDK ?= ../../../build/kernel-sdk
LIBOS_SUPPORT ?= ../common
PROFILE ?= preempt
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
SOURCES ?= main.c
FLAGS := -m32 -march=i686 -mno-mmx -mno-sse -mno-sse2 -msoft-float -std=c11 -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-common -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-omit-frame-pointer -g3 -O2 -Wall -Wextra -Werror -Wpedantic
ifeq ($(PROFILE),coop)
PROFILE_FLAGS := -DLIBOS_COOPERATIVE
else ifneq ($(PROFILE),preempt)
$(error PROFILE must be preempt or coop)
endif
ifneq ($(MIN_MINOR),)
PROFILE_FLAGS += -DLIBOS_MIN_MINOR=$(MIN_MINOR)
endif
.PHONY: all check clean
all: check $(OUT)/libos.elf
check:
	@command -v $(CC) >/dev/null || { echo "Missing $(CC): install GCC with i686/-m32 support" >&2; exit 1; }
	@command -v $(LD) >/dev/null || { echo "Missing $(LD): install GNU binutils" >&2; exit 1; }
	@test -r "$(SDK)/include/abi/elf.h" || { echo "Set SDK to an exported Kernel ABI SDK (make sdk in Liberums)" >&2; exit 1; }
# A changed profile/policy must rebuild the runtime, even without make clean.
.PHONY: config-check
config-check:
$(OUT)/.config: config-check
	@mkdir -p $(OUT)
	@printf '%s\n' '$(PROFILE) $(MIN_MINOR) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(CC) $(LD)' > $(OUT)/.config.next
	@if ! cmp -s $(OUT)/.config.next $@; then mv $(OUT)/.config.next $@; else rm -f $(OUT)/.config.next; fi
OBJECTS := $(patsubst %.c,$(OUT)/%.o,$(SOURCES)) $(OUT)/runtime.o $(OUT)/start.o
$(OUT)/%.o: %.c $(OUT)/.config $(wildcard *.h) $(wildcard $(SDK)/include/abi/*.h) $(LIBOS_SUPPORT)/runtime.h Makefile $(LIBOS_SUPPORT)/libos.mk
	mkdir -p $(dir $@)
	$(CC) -I"$(SDK)/include" -I"$(LIBOS_SUPPORT)" $(FLAGS) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
$(OUT)/runtime.o: $(LIBOS_SUPPORT)/runtime.c $(OUT)/.config $(LIBOS_SUPPORT)/runtime.h $(wildcard $(SDK)/include/abi/*.h) Makefile $(LIBOS_SUPPORT)/libos.mk
	mkdir -p $(OUT)
	$(CC) -I"$(SDK)/include" -I"$(LIBOS_SUPPORT)" $(FLAGS) $(PROFILE_FLAGS) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@
$(OUT)/start.o: $(LIBOS_SUPPORT)/start.S
	mkdir -p $(OUT)
	$(CC) -m32 -march=i686 -g3 -c $< -o $@
$(OUT)/libos.elf: $(OBJECTS) $(LIBOS_SUPPORT)/linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T "$(LIBOS_SUPPORT)/linker.ld" -Map=$(OUT)/libos.map $(OBJECTS) -o $@
clean:
	rm -rf $(OUT)
-include $(OBJECTS:.o=.d)
