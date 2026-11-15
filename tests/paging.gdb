# SPDX-License-Identifier: GPL-3.0-or-later
set pagination off
set confirm off
set remotetimeout 5
hbreak *kernel_ready
continue
python
import gdb
import struct
def value(expression):
    return int(gdb.parse_and_eval(expression)) & 0xffffffff
def check(condition, message):
    if not condition:
        raise gdb.GdbError(message)
check(value('$eip') == value('&kernel_ready'), 'runtime checkpoint not reached')
check(value('$cr0') & 0x80010001 == 0x80010001, 'CR0 PG/WP/PE missing')
check(value('$eflags') & 0x200 == 0, 'external interrupts unexpectedly enabled')
directory = value('&page_directory')
tables = value('&page_tables')
check(directory % 4096 == 0 and tables % 4096 == 0, 'unaligned page structures')
check(value('$cr3') == directory, 'unexpected CR3')
inferior = gdb.selected_inferior()
directory_words = struct.unpack('<1024I', inferior.read_memory(directory, 4096))
table_words = struct.unpack('<16384I', inferior.read_memory(tables, 65536))
page_states = bytes(inferior.read_memory(value('&states'), 16384))
for i, entry in enumerate(directory_words):
    if i < 16:
        check(entry & 0xfffff000 == tables + i * 4096, 'wrong PDE target')
        check(entry & 7 == 3, 'PDE permission mismatch')
    else:
        check(entry == 0, 'mapping beyond the 64 MiB limit')
def pte(address):
    return table_words[address >> 12]
check(pte(0) == 0, 'null page is mapped')
start = value('&__kernel_start')
ro_end = value('&__kernel_readonly_end')
end = value('&__kernel_end')
for address in range(start, end, 4096):
    entry = pte(address)
    check(entry & 0xfffff000 == address, 'kernel is not identity-mapped')
    check(entry & 7 == (1 if address < ro_end else 3), 'kernel PTE permissions')
    check(page_states[address >> 12] == 3, 'kernel page is allocatable')
for address in range(directory, directory + 4096, 4096):
    check(page_states[address >> 12] == 3, 'page directory not reserved')
for address in range(tables, tables + 16 * 4096, 4096):
    check(page_states[address >> 12] == 3, 'page table not reserved')
for i in range(value('boot.reserved_count')):
    begin = value('boot.reserved[%d].begin' % i)
    end = value('boot.reserved[%d].end' % i)
    for address in range(begin & ~4095, end, 4096):
        check(pte(address) & 1 == 1, 'boot data is unmapped')
        check(page_states[address >> 12] != 2, 'boot data is free')
for entry in table_words:
    check(entry & 4 == 0, 'user-accessible PTE')
print('PASS: actual CR0 PG/WP, CR3, all PDEs/PTEs, kernel and boot reservations')
end
info registers cr0 cr2 cr3 eip esp
delete breakpoints
detach
quit
