# Bitmap object-pool LibOS

The kernel owns page tables and checks page handles. This user program chooses
one VA, fixed object size and a first-free bitmap. Default: 64 objects × 64 bytes
in one real page. It fills/checks every byte, checks exhaustion and invalid/double
free, then unmaps/releases the page and checks that QUERY on the old handle fails.

```sh
make SDK=/path/to/kernel-sdk
make SDK=/path/to/kernel-sdk CFLAGS=-DOBJECT_BYTES=128  # 32 objects; same kernel
```

Copy common/ as a sibling, or generate a standalone project. No host libc or Ael code.
The normal program runs bounded CPU work without yielding, then exits; IRQ0 may
interrupt it at any instruction. Private lesson_command/lesson_gate are tutorial
workload inputs, not privilege. Modes 1/2/3 demonstrate idle ACK, lost-page safe
exit 96 and a real old-pointer #PF with the explicitly enabled trusted diagnostic.
Mode 4 faults, mode 5 holds for a multi-domain experiment. Normal mode is zero.
QUERY is not a pin, and active pointers cannot be transparently recovered.

[Resources](../../../docs/libos-development/03-resources.md),
[policy](../../../docs/libos-development/04-policy.md),
[revocation experiment](../../../docs/libos-development/06-revocation.md).
