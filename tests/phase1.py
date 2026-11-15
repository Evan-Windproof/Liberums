#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual make run/debug targets, preserving failure and test logs."""
import argparse
import os
from pathlib import Path
import shutil
import signal
import socket
import struct
import subprocess
import sys
import time
from contextlib import contextmanager

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
ELF = BUILD / "liberums.elf"
BANNER = (
    "Liberums: boot successful\n"
    "Phase 1: x86 32-bit C11 kernel, COM1 ready\n"
    "Multiboot handoff accepted; interrupts disabled; halting.\n"
)


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check_elf():
    data = ELF.read_bytes()
    require(data[:7] == b"\x7fELF\x01\x01\x01", "expected little-endian ELF32")
    header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    kind, machine, _, entry, phoff = header[:5]
    require(kind == 2 and machine == 3, "expected i386 executable")
    headers = [struct.unpack_from("<IIIIIIII", data, phoff + i * header[8])
               for i in range(header[9])]
    require(any(p[0] == 1 and p[6] & 1 and p[2] <= entry < p[2] + p[5]
                for p in headers), "entry is outside executable LOAD segments")
    require(not any(p[0] == 2 for p in headers), "unexpected dynamic linking")
    require(not any(p[0] == 1 and p[6] & 3 == 3 for p in headers),
            "unexpected writable/executable ELF segment")
    candidates = [i for i in range(0, min(len(data), 8192) - 11, 4)
                  if struct.unpack_from("<I", data, i)[0] == 0x1badb002]
    require(len(candidates) == 1, "missing/ambiguous Multiboot header in first 8 KiB")
    magic, flags, checksum = struct.unpack_from("<III", data, candidates[0])
    require(flags == 3 and (magic + flags + checksum) & 0xffffffff == 0,
            "invalid Multiboot flags/checksum")
    undefined = subprocess.check_output(["nm", "-u", str(ELF)], text=True)
    require(not undefined.strip(), "unresolved symbols: " + undefined)
    print(f"PASS: ELF32/i386, entry {entry:#x}, Multiboot at file offset {candidates[0]:#x}")


@contextmanager
def emulator(target, qemu, port=None, append=None, label=None, initrd=None):
    log_path = BUILD / ("boot-test.log" if target == "run" else "debug-serial.log")
    if label is not None:
        log_path = BUILD / (label + "-serial.log")
    command = ["make", "--no-print-directory", target, "QEMU=" + qemu]
    if initrd is not None:
        command.append("QEMU_INITRD=" + str(initrd))
    if append is not None:
        command.append("QEMU_APPEND=" + append)
    if port is not None:
        command.append(f"GDB_PORT={port}")
    with log_path.open("w") as log:
        process = subprocess.Popen(command, cwd=ROOT, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            yield process, log_path
        finally:
            # Only terminate the process group created by this test, never other VMs.
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait(timeout=3)


def wait_for(process, log_path, predicate, description):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        text = log_path.read_text(errors="replace").replace("\r\n", "\n")
        require(process.poll() is None, f"QEMU exited early; see {log_path}\n{text}")
        if predicate(text):
            return
        time.sleep(0.05)
    raise RuntimeError(f"timeout waiting for {description}; see {log_path}")


def check_boot(qemu):
    with emulator("run", qemu) as (process, log):
        wait_for(process, log, lambda text: BANNER in text, "complete serial banner")
    print("PASS: make run emitted all three expected serial lines")


def check_gdb(qemu, gdb):
    # Pick a free loopback port, avoiding interference with a user's debug VM.
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    with emulator("debug", qemu, port) as (process, log):
        def listening(_text):
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.2):
                    return True
            except OSError:
                return False
        wait_for(process, log, listening, "QEMU GDB stub")
        environment = dict(os.environ, DEBUGINFOD_URLS="")
        result = subprocess.run(
            [gdb, "-q", "-nx", "-batch", str(ELF),
             "-ex", "set architecture i386",
             "-ex", f"target remote 127.0.0.1:{port}",
             "-x", str(ROOT / "tests/entry.gdb")],
            cwd=ROOT, env=environment, text=True, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=20)
        (BUILD / "gdb-test.log").write_text(result.stdout)
        require(result.returncode == 0, "GDB failed; see build/gdb-test.log\n" + result.stdout)
        require("PASS: _start reached" in result.stdout
                and "PASS: kernel_main reached" in result.stdout,
                "GDB did not execute both entry checks")
        wait_for(process, log, lambda text: BANNER in text, "serial output after GDB detach")
    print("PASS: make debug + GDB verified _start and kernel_main; boot resumed successfully")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-i386")
    parser.add_argument("--gdb", default="gdb")
    parser.add_argument("--mode", choices=("all", "boot", "gdb"), default="all")
    args = parser.parse_args()
    check_elf()
    require(shutil.which(args.qemu), f"QEMU executable not found: {args.qemu}")
    if args.mode in ("all", "boot"):
        check_boot(args.qemu)
    if args.mode in ("all", "gdb"):
        require(shutil.which(args.gdb), f"GDB executable not found: {args.gdb}")
        check_gdb(args.qemu, args.gdb)


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
