# Independent text console

The user program probes the optional VGA/PS2 interfaces, acquires explicitly
granted handles, maps the fixed VGA page and interprets raw set-1 bytes itself.
Echo, editing, colours, scrolling and help/clear/about/status/exit run in Ring 3.
`exit` releases both handles before normal domain exit.

```sh
# In Liberums after make libos-examples:
make run-teaching-console QEMU_DISPLAY=gtk
make test-libos-examples  # none backend: QMP -> real PS/2 IRQ1 -> Ring3 -> VGA
```

Generic headless launch: use multi-external with multi-vga=1 multi-keyboard=1
and this ELF as module 1. Device INFO does not grant access. Without grants it
fails with a nonzero application status; it cannot enable arbitrary port I/O.
Only the US set-1 teaching layout is decoded. There is no Shell, filesystem or GUI.

`core.c`/`core.h` are a documented teaching adaptation of Liberums's own
`user/console/core.*` at `2a66612b2823fe7b010257373b23f9aa38252eb0`, preserving
GPL-3.0-or-later notices. The exported copy makes this project independent of
that source tree; it adds a terminating exit command. No Aelthuvor code is copied.
[Optional devices lesson](../../../docs/libos-development/07-devices.md).
