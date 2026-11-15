# SPDX-License-Identifier: GPL-3.0-or-later
# Caller loads the ELF and connects to QEMU's paused GDB stub first.
set pagination off
set confirm off
set remotetimeout 5
hbreak *_start
continue
python
import gdb
def reg(name):
    return int(gdb.parse_and_eval("$" + name)) & 0xffffffff
def check(condition, message):
    if not condition:
        raise gdb.GdbError(message)
check(reg("eip") == int(gdb.parse_and_eval("&_start")), "missed assembly entry")
check(reg("eax") == 0x2badb002, "invalid Multiboot EAX")
check(reg("ebx") != 0, "missing Multiboot information")
boot_info = reg("ebx")
print("PASS: _start reached with Multiboot magic and information pointer")
end
info registers eip eax ebx
delete breakpoints
hbreak *kernel_main
continue
python
check(reg("eip") == int(gdb.parse_and_eval("&kernel_main")), "missed C entry")
check(reg("cr0") & 1, "protected mode is disabled")
check(not (reg("cr0") & 0x80000000), "unexpected paging")
check(not (reg("eflags") & 0x600), "interrupt or direction flag unexpectedly set")
check(reg("cs") == 8 and reg("ds") == 16 and reg("ss") == 16, "invalid kernel segments")
check(reg("esp") % 16 == 12, "invalid i386 call stack alignment")
bottom = int(gdb.parse_and_eval("&kernel_stack_bottom"))
top = int(gdb.parse_and_eval("&kernel_stack_top"))
check(bottom < reg("esp") < top, "stack outside reserved region")
check(int(gdb.parse_and_eval("*(unsigned int *)($esp + 4)")) == 0x2badb002, "bad first C argument")
check(int(gdb.parse_and_eval("*(unsigned int *)($esp + 8)")) == boot_info, "bad second C argument")
print("PASS: kernel_main reached; mode, segments, flags, stack and C arguments verified")
end
info registers eip esp cs ds ss cr0 eflags
x/3wx $esp
delete breakpoints
detach
quit
