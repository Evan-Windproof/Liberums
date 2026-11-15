#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent normal, fatal-exception and malformed-handoff QEMU guests."""
import argparse
import os
import re
import socket
import subprocess
import sys

from phase1 import BUILD, ELF, ROOT, BANNER, emulator, require, wait_for

READY = "Phase 2: protected memory foundations ready"


def connect_and_run(process, log, port, gdb, commands, label):
    def listening(_):
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                return True
        except OSError:
            return False
    wait_for(process, log, listening, "GDB stub")
    script = BUILD / (label + ".gdb")
    script.write_text("set pagination off\nset confirm off\nset remotetimeout 5\n" + commands)
    result = subprocess.run(
        [gdb, "-q", "-nx", "-batch", str(ELF), "-ex", "set architecture i386",
         "-ex", f"target remote 127.0.0.1:{port}", "-x", str(script)],
        cwd=ROOT, env=dict(os.environ, DEBUGINFOD_URLS=""), text=True,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=60)
    (BUILD / (label + "-gdb.log")).write_text(result.stdout)
    require(result.returncode == 0, f"{label}: GDB failed\n{result.stdout}")
    return result.stdout


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def symbols():
    output = subprocess.check_output(["nm", "-n", str(ELF)], text=True)
    return {fields[2]: int(fields[0], 16) for line in output.splitlines()
            if len(fields := line.split()) == 3}


def normal(qemu, gdb):
    port = free_port()
    with emulator("debug", qemu, port, label="paging") as (process, log):
        output = connect_and_run(process, log, port, gdb,
                                 (ROOT / "tests/paging.gdb").read_text(), "paging")
        require("PASS: actual CR0 PG/WP" in output, "paging checks not executed")
        wait_for(process, log, lambda text: READY in text and BANNER in text, "normal Phase 2 boot")
    print("PASS: GDB verified enabled PG/WP, identity map, read-only kernel and boot data")
    with emulator("run", qemu, append="selftest=pages", label="pages") as (process, log):
        wait_for(process, log, lambda text: "SELFTEST: physical reservations, exhaustion, uniqueness and reuse passed" in text
                 and READY in text and BANNER in text, "guest page-state tests")
    print("PASS: QEMU physical page reservations, exhaustion, uniqueness, free and reuse")
    module = BUILD / "test-module.bin"
    module.write_bytes(b"Liberums reservation test\n" * 400)
    port = free_port()
    with emulator("debug", qemu, port, append="selftest=pages", label="module", initrd=module) as (process, log):
        commands = (ROOT / "tests/paging.gdb").read_text()
        module_check = f"""python
check(value('boot.reserved_count') >= 7, 'module reservations missing')
module_begin = value('boot.reserved[5].begin')
module_end = value('boot.reserved[5].end')
check(module_end - module_begin == {module.stat().st_size}, 'module extent corrupted')
check(bytes(inferior.read_memory(module_begin, 26)) == b'Liberums reservation test\\n', 'module contents corrupted')
end
"""
        commands = commands.replace('info registers cr0', module_check + 'info registers cr0')
        output = connect_and_run(process, log, port, gdb, commands, "module")
        require("PASS: actual CR0 PG/WP" in output, "module mapping checks not executed")
        wait_for(process, log, lambda text: "SELFTEST: physical reservations" in text
                 and READY in text and BANNER in text, "module reservation guest")
    print("PASS: actual Multiboot module, descriptor and strings remain reserved and mapped")


def fatal(qemu, gdb, names):
    cases = [
        ("ud", 6, 0, "fault_ud", None),
        ("divide", 0, 0, "fault_divide", None),
        ("gp", 13, 0x18, "fault_gp", None),
        ("pf-read", 14, 0, "fault_pf_read", 0x4000000),
        ("pf-null", 14, 0, "fault_pf_null", 0),
        ("ro-write", 14, 3, "fault_ro_write", names["__kernel_start"]),
        ("tlb", 14, 0, "fault_unmapped", None),
    ]
    for option, vector, error, site, address in cases:
        label = "fault-" + option
        port = free_port()
        with emulator("debug", qemu, port, append="selftest=" + option, label=label) as (process, log):
            commands = """hbreak *kernel_stop
continue
python
import gdb
if int(gdb.parse_and_eval('$eip')) != int(gdb.parse_and_eval('&kernel_stop')):
    raise gdb.GdbError('did not reach explicit stop')
if int(gdb.parse_and_eval('$cr0')) & 0x80010001 != 0x80010001:
    raise gdb.GdbError('fault did not occur with paging and WP enabled')
print('hardware-cr2=0x%08x' % int(gdb.parse_and_eval('$cr2')))
end
delete breakpoints
detach
quit
"""
            output = connect_and_run(process, log, port, gdb, commands, label)
            wait_for(process, log, lambda text: "HALTED\n" in text, "fatal report")
            text = log.read_text()
            for key, expected in (("vector", vector), ("error", error), ("eip", names[site])):
                require(f"{key}=0x{expected:08x}" in text, f"{label}: wrong {key}; see {log}")
            require(READY not in text and "boot successful" not in text, f"{label}: continued after fatal fault")
            require("PANIC: unrecoverable CPU exception" in text, f"{label}: missing panic")
            if option == "divide":
                require("eax=0x13579bdf" in text, "register save corrupted")
            if option == "tlb":
                match = re.search(r"test-address=(0x[0-9a-f]+)", text)
                require(match is not None, "TLB warm-up not executed")
                address = int(match.group(1), 16)
            if vector == 14:
                require(f"cr2=0x{address:08x}" in text, f"{label}: wrong reported CR2")
                require(f"hardware-cr2=0x{address:08x}" in output, f"{label}: wrong hardware CR2")
                expected = "PF: protection write supervisor" if error == 3 else "PF: not-present read supervisor"
                require(expected in text, f"{label}: wrong PF interpretation")
        print(f"PASS: independent {label} guest; vector/error/EIP and fatal stop verified")


def malformed(qemu, gdb):
    cases = {
        "wrong-magic": "set $eax = 0",
        "invalid-info-address": "set $ebx = 0xfffffff0",
        "empty-map": "set {unsigned int}($ebx+44) = 0",
        "module-count": "set {unsigned int}($ebx+20) = 0xffffffff",
        "module-pointer": "set {unsigned int}($ebx+20) = 1\nset {unsigned int}($ebx+24) = 0xfffffff0",
        "unterminated-commandline": "set {unsigned int}($ebx+16) = 0x70000\npython gdb.selected_inferior().write_memory(0x70000, b'x' * 128)",
        "missing-map": "set {unsigned int}$ebx = 0",
        "unsupported-flags": "set {unsigned int}$ebx = 0x50",
        "truncated-map": "set {unsigned int}($ebx+44) = 3",
        "pointer-overflow": "set {unsigned int}($ebx+48) = 0xfffffff0",
        "record-overflow": "set $map = *(unsigned int *)($ebx+48)\nset {unsigned int}$map = 0xffffffff",
        "range-overflow": "set $map = *(unsigned int *)($ebx+48)\nset {unsigned int}($map+4) = 0xfffffff0\nset {unsigned int}($map+8) = 0xffffffff",
    }
    for name, mutation in cases.items():
        label = "reject-" + name
        port = free_port()
        with emulator("debug", qemu, port, label=label) as (process, log):
            commands = "hbreak *_start\ncontinue\n" + mutation + "\ndelete breakpoints\n"
            commands += """hbreak *kernel_stop
continue
python
import gdb
if int(gdb.parse_and_eval('$eip')) != int(gdb.parse_and_eval('&kernel_stop')):
    raise gdb.GdbError('invalid input did not stop safely')
if int(gdb.parse_and_eval('$cr0')) & 0x80000000:
    raise gdb.GdbError('invalid input reached paging')
if int(gdb.parse_and_eval('initialized')):
    raise gdb.GdbError('invalid map reached page allocation')
end
delete breakpoints
detach
quit
"""
            connect_and_run(process, log, port, gdb, commands, label)
            wait_for(process, log, lambda text: "HALTED\n" in text, "safe boot-data rejection")
            text = log.read_text()
            require("PANIC: invalid or unsupported Multiboot memory information" in text,
                    f"{label}: wrong failure")
            require("EXCEPTION" not in text and READY not in text, f"{label}: unsafe continuation")
        print(f"PASS: malformed {name} rejected before allocation or paging")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--gdb", default="gdb")
    args = parser.parse_args()
    normal(args.qemu, args.gdb)
    fatal(args.qemu, args.gdb, symbols())
    malformed(args.qemu, args.gdb)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
