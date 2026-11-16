# SPDX-License-Identifier: GPL-3.0-or-later
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
QEMU ?= qemu-system-i386
GDB ?= gdb
PYTHON ?= python3
HOST_CC ?= gcc
GDB_PORT ?= 1234
QEMU_INITRD ?=
AELTHUVOR ?= ../Aelthuvor
QEMU_APPEND ?=

BUILD := build
KERNEL := $(BUILD)/liberums.elf
C_SOURCES := kernel/main.c kernel/serial.c kernel/panic.c arch/x86/idt.c \
             kernel/memory_map.c kernel/boot_info.c kernel/physical_pages.c \
             arch/x86/paging.c kernel/selftest.c arch/x86/gdt.c kernel/domain.c kernel/syscall.c \
             kernel/capability.c kernel/resource.c tests/resource_pressure.c \
             kernel/elf_loader.c kernel/external.c arch/x86/user_space.c tests/elf_pressure.c \
             arch/x86/pic.c arch/x86/pit.c kernel/tick.c kernel/timer.c kernel/scheduler.c \
             kernel/revoke.c tests/phase6_runtime.c kernel/vga.c kernel/vga_device.c tests/phase71_runtime.c arch/x86/ps2.c kernel/keyboard.c tests/phase72_runtime.c kernel/device_grants.c kernel/external_launch.c tests/runtime_checks.c
ASM_SOURCES := arch/x86/keyboard_irq.S arch/x86/irq.S  arch/x86/boot.S arch/x86/isr.S arch/x86/test_faults.S arch/x86/user_context.S arch/x86/user_images.S
OBJECTS := $(addprefix $(BUILD)/,$(C_SOURCES:.c=.o) $(ASM_SOURCES:.S=.o))
ARCH_FLAGS := -m32 -march=i686 -mno-mmx -mno-sse -mno-sse2 -msoft-float
KERNEL_CFLAGS := $(ARCH_FLAGS) -std=c11 -ffreestanding -fno-builtin \
                 -fno-pic -fno-pie -fno-stack-protector -fno-common \
                 -fno-asynchronous-unwind-tables -fno-unwind-tables \
                 -fno-omit-frame-pointer -Wall -Wextra -Werror -Wpedantic -g3 -O2
QEMU_FLAGS := -machine pc -accel tcg -cpu qemu32 -m 32M -smp 1 \
              -display none -serial stdio -monitor none -nic none \
              -no-reboot -no-shutdown

.PHONY: all run debug test test-memory clean
all: $(KERNEL)

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.S Makefile
	mkdir -p $(dir $@)
	$(CC) $(ARCH_FLAGS) -ffreestanding -fno-pic -fno-pie -g3 -c $< -o $@

$(BUILD)/%.o: %.c Makefile
	mkdir -p $(dir $@)
	$(CC) -Iinclude $(CPPFLAGS) $(KERNEL_CFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/user/%.o: user/%.c user/api.h include/kernel/user_abi.h include/kernel/resource_abi.h $(wildcard include/abi/*.h) Makefile
	mkdir -p $(dir $@)
	$(CC) -Iinclude $(KERNEL_CFLAGS) -c $< -o $@

$(BUILD)/user/%.o: user/%.S Makefile
	mkdir -p $(dir $@)
	$(CC) $(ARCH_FLAGS) -c $< -o $@

$(BUILD)/user_%.elf: $(BUILD)/user/test_%.o $(BUILD)/user/program.o $(BUILD)/user/entry_checks.o user/linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T user/linker.ld $(filter %.o,$^) -o $@

$(BUILD)/user_%.bin: $(BUILD)/user_%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/user/resource_BASIC.o: RESOURCE_VARIANT = -DRESOURCE_IMAGE_BASIC
$(BUILD)/user/resource_TRANSFER.o: RESOURCE_VARIANT = -DRESOURCE_IMAGE_TRANSFER
$(BUILD)/user/resource_FAULT.o: RESOURCE_VARIANT = -DRESOURCE_IMAGE_FAULT

$(addprefix $(BUILD)/user/resource_,BASIC.o TRANSFER.o FAULT.o): user/resource_test.c user/api.h include/kernel/resource_abi.h include/kernel/capability.h $(wildcard include/abi/*.h) Makefile
	mkdir -p $(dir $@)
	$(CC) -Iinclude $(KERNEL_CFLAGS) -Os $(RESOURCE_VARIANT) -c $< -o $@

$(BUILD)/resource_%.elf: $(BUILD)/user/resource_entry.o $(BUILD)/user/resource_%.o $(BUILD)/user/resource_faults.o user/linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T user/linker.ld $(filter %.o,$^) -o $@

$(BUILD)/resource_%.bin: $(BUILD)/resource_%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/arch/x86/user_images.o: $(BUILD)/user_a.bin $(BUILD)/user_b.bin \
    $(BUILD)/resource_BASIC.bin $(BUILD)/resource_TRANSFER.bin $(BUILD)/resource_FAULT.bin

.SECONDARY:

$(KERNEL): $(OBJECTS) arch/x86/linker.ld Makefile
	$(LD) -m elf_i386 -nostdlib --build-id=none -T arch/x86/linker.ld \
		-Map=$(BUILD)/liberums.map $(LDFLAGS) $(OBJECTS) -o $@

run: $(KERNEL)
	$(QEMU) $(QEMU_FLAGS) -kernel $(KERNEL) $(if $(QEMU_APPEND),-append "$(QEMU_APPEND)") $(if $(QEMU_INITRD),-initrd "$(QEMU_INITRD)")

debug: $(KERNEL)
	$(QEMU) $(QEMU_FLAGS) -kernel $(KERNEL) $(if $(QEMU_APPEND),-append "$(QEMU_APPEND)") $(if $(QEMU_INITRD),-initrd "$(QEMU_INITRD)") -S -gdb tcp:127.0.0.1:$(GDB_PORT)

$(BUILD)/memory-tests: tests/memory_tests.c kernel/memory_map.c kernel/physical_pages.c include/kernel/memory_map.h include/kernel/physical_pages.h Makefile | $(BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g \
		-fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude \
		tests/memory_tests.c kernel/memory_map.c kernel/physical_pages.c -o $@

$(BUILD)/capability-tests: tests/capability_tests.c kernel/capability.c include/kernel/capability.h include/kernel/resource_abi.h $(wildcard include/abi/*.h) Makefile | $(BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g -fsanitize=address,undefined \
		-Iinclude tests/capability_tests.c kernel/capability.c -o $@

$(BUILD)/elf-tests: tests/elf_tests.c kernel/elf_loader.c include/kernel/elf_loader.h $(wildcard include/abi/*.h) Makefile | $(BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g -fsanitize=address,undefined \
		-Iinclude tests/elf_tests.c kernel/elf_loader.c -o $@

.PHONY: external-fixtures
external-fixtures:
	$(MAKE) -C tests/fixtures/external_user SDK="$(abspath include)" OUT="$(abspath $(BUILD)/external)" CC="$(CC)" LD="$(LD)"

test-memory: $(BUILD)/memory-tests $(BUILD)/capability-tests
	./$(BUILD)/memory-tests
	./$(BUILD)/capability-tests

test: $(KERNEL) test-memory $(BUILD)/elf-tests external-fixtures
	$(PYTHON) tests/phase1.py --qemu "$(QEMU)" --gdb "$(GDB)"
	$(PYTHON) tests/phase2.py --qemu "$(QEMU)" --gdb "$(GDB)"
	$(PYTHON) tests/phase3.py --qemu "$(QEMU)" --gdb "$(GDB)"
	$(PYTHON) tests/phase4.py --qemu "$(QEMU)" --gdb "$(GDB)"
	$(PYTHON) tests/phase5.py --qemu "$(QEMU)" --gdb "$(GDB)"
	$(PYTHON) tests/phase6.py --qemu "$(QEMU)" --gdb "$(GDB)"
	$(PYTHON) tests/phase71.py --qemu "$(QEMU)" --gdb "$(GDB)"
	$(PYTHON) tests/phase72.py --qemu "$(QEMU)" --gdb "$(GDB)"
	$(PYTHON) tests/phase73.py --qemu "$(QEMU)" --gdb "$(GDB)"

.PHONY: test-libos
test-libos: $(KERNEL)
	$(MAKE) -C "$(AELTHUVOR)" integration LIBERUMS="$(CURDIR)" QEMU="$(QEMU)" GDB="$(GDB)"

clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d)

$(BUILD)/phase6-entry.o: tests/fixtures/preempt/entry.S Makefile | $(BUILD)
	$(CC) $(ARCH_FLAGS) -g3 -c $< -o $@
$(BUILD)/phase6-program.o: tests/fixtures/preempt/program.c $(wildcard include/abi/*.h) Makefile | $(BUILD)
	$(CC) -Iinclude $(KERNEL_CFLAGS) -Os -c $< -o $@
$(BUILD)/phase6-user.elf: $(BUILD)/phase6-entry.o $(BUILD)/phase6-program.o user/linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T user/linker.ld $(filter %.o,$^) -o $@
$(BUILD)/phase6-user.bin: $(BUILD)/phase6-user.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/arch/x86/user_images.o: $(BUILD)/phase6-user.bin

$(BUILD)/timer-tests: tests/timer_tests.c kernel/tick.c include/kernel/tick.h Makefile | $(BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g -fsanitize=address,undefined -Iinclude tests/timer_tests.c kernel/tick.c -o $@
$(BUILD)/revoke-tests: tests/revoke_tests.c kernel/revoke.c kernel/resource.c kernel/capability.c kernel/tick.c $(wildcard include/kernel/*.h include/abi/*.h) Makefile | $(BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g -no-pie -fsanitize=address,undefined -Iinclude tests/revoke_tests.c kernel/capability.c kernel/tick.c -o $@
.PHONY: test-phase6-native test-phase6
test-phase6-native: $(BUILD)/timer-tests $(BUILD)/revoke-tests
	./$(BUILD)/timer-tests
	./$(BUILD)/revoke-tests
test-phase6: $(KERNEL)
	$(PYTHON) tests/phase6.py --qemu "$(QEMU)" --gdb "$(GDB)"

# Graphical display is opt-in; original run/debug stay headless.
QEMU_DISPLAY ?= gtk
.PHONY: run-vga-diag
run-vga-diag: $(KERNEL)
	$(QEMU) $(subst -display none,-display $(QEMU_DISPLAY),$(QEMU_FLAGS)) -kernel $(KERNEL) -append "vga-diag"

$(BUILD)/vga-user.elf: $(BUILD)/user/vga_console.o user/linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T user/linker.ld $< -o $@
$(BUILD)/vga-user.bin: $(BUILD)/vga-user.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/arch/x86/user_images.o: $(BUILD)/vga-user.bin
.PHONY: run-vga test-vga
run-vga: $(KERNEL)
	$(QEMU) $(subst -display none,-display $(QEMU_DISPLAY),$(QEMU_FLAGS)) -kernel $(KERNEL) -append "vga-console $(QEMU_APPEND)"
test-vga: $(KERNEL)
	$(PYTHON) tests/phase71.py --qemu "$(QEMU)" --gdb "$(GDB)"

$(BUILD)/vga-device-tests: tests/vga_device_tests.c kernel/vga_device.c kernel/capability.c $(wildcard include/abi/*.h include/kernel/*.h) | $(BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g -no-pie -fsanitize=address,undefined -Iinclude tests/vga_device_tests.c kernel/capability.c -o $@
.PHONY: test-vga-native
test-vga-native: $(BUILD)/vga-device-tests
	./$(BUILD)/vga-device-tests
test-vga: test-vga-native

test: test-vga-native
.PHONY: test-vga-compat
test-vga-compat: $(KERNEL)
	$(PYTHON) tests/phase71_compat.py --aelthuvor "$(AELTHUVOR)" --qemu "$(QEMU)" --gdb "$(GDB)"

$(BUILD)/user/interactive_console.o: user/interactive_console.c $(wildcard include/abi/*.h) Makefile
	mkdir -p $(dir $@)
	$(CC) -Iinclude $(KERNEL_CFLAGS) -Os -c $< -o $@
$(BUILD)/interactive-user.elf: $(BUILD)/user/interactive_console.o $(BUILD)/user/console/core.o user/linker.ld
	$(LD) -m elf_i386 -nostdlib --build-id=none -T user/linker.ld $(filter %.o,$^) -o $@
$(BUILD)/interactive-user.bin: $(BUILD)/interactive-user.elf
	$(OBJCOPY) -O binary $< $@
$(BUILD)/arch/x86/user_images.o: $(BUILD)/interactive-user.bin
.PHONY: run-console test-keyboard
run-console: $(KERNEL)
	$(QEMU) $(subst -display none,-display $(QEMU_DISPLAY),$(QEMU_FLAGS)) -kernel $(KERNEL) -append "interactive-console $(QEMU_APPEND)"
test-keyboard: $(KERNEL)
	$(PYTHON) tests/phase72.py --qemu "$(QEMU)" --gdb "$(GDB)"

$(BUILD)/keyboard-tests: tests/keyboard_tests.c kernel/keyboard.c kernel/capability.c $(wildcard include/abi/*.h include/kernel/*.h include/arch/x86/*.h) | $(BUILD)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g -no-pie -fsanitize=address,undefined -Iinclude tests/keyboard_tests.c kernel/capability.c -o $@
.PHONY: test-keyboard-native
test-keyboard-native: $(BUILD)/keyboard-tests
	./$(BUILD)/keyboard-tests
test-keyboard: test-keyboard-native

test: test-keyboard-native

$(BUILD)/user/console/core.o: user/console/core.c user/console/core.h $(wildcard include/abi/*.h) Makefile
	mkdir -p $(dir $@)
	$(CC) -Iinclude $(KERNEL_CFLAGS) -Os -c $< -o $@
$(BUILD)/user/interactive_console.o: user/console/core.h
.PHONY: external-console
external-console:
	$(MAKE) -C user/external_console SDK="$(abspath include)" OUT="$(abspath build/external)" CROSS_COMPILE="$(CROSS_COMPILE)"

CONSOLE_ELF ?= build/external/console.elf
.PHONY: run-external-console
run-external-console: external-console
	test -r "$(KERNEL)"
	test -r "$(CONSOLE_ELF)"
	$(QEMU) $(subst -display none,-display $(QEMU_DISPLAY),$(QEMU_FLAGS)) -kernel $(KERNEL) -append "external-console $(QEMU_APPEND)" -initrd "$(CONSOLE_ELF)"

$(BUILD)/device-grants-tests: tests/device_grants_tests.c kernel/device_grants.c kernel/vga_device.c kernel/keyboard.c kernel/capability.c $(wildcard include/kernel/*.h include/abi/*.h)
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -g -no-pie -fsanitize=address,undefined -Iinclude $(filter %.c,$^) -o $@
.PHONY: test-device-grants
test-device-grants: $(BUILD)/device-grants-tests
	./$(BUILD)/device-grants-tests

.PHONY: test-external-console
test-external-console: $(KERNEL) test-device-grants
	$(PYTHON) tests/phase73.py --qemu "$(QEMU)" --gdb "$(GDB)"

test: test-device-grants
