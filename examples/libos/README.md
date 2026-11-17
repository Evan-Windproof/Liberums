# Independent LibOS examples

These GPL-3.0-or-later programs use only the exported Liberums Kernel ABI SDK.
They do not link Aelthuvor, host libc, or kernel private headers.

```sh
make sdk libos-examples                 # from the Liberums source root
make run-libos                         # minimal, serial-only, then guest halts
make run-libos LIBOS_ELF=build/examples/memory-policy/libos.elf
make run-teaching-console QEMU_DISPLAY=gtk  # local GTK/WSLg required
make test-libos-examples                # actual headless QEMU/GDB + PS/2 input
```

Each example has its own Makefile and application code. `common/` is **user code**:
our assembly entry, runtime, linker script and small Make fragment. To move an
example, copy it with common as a sibling and pass `SDK=/path/to/exported-sdk`,
or use `scripts/new_libos.py` to create a self-contained project with local support/ and sdk/.

[Minimal](minimal/README.md), [bitmap pool](memory-policy/README.md),
[console](console/README.md), [ten lessons](../../docs/libos-development/README.md).
Application functions such as libos_main are this example's API, not kernel syscalls.
