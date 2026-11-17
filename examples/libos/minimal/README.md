# Minimal LibOS

Our own assembly `_start` calls our C runtime, which probes version/features and
limits before invoking `libos_main`. The application logs a short message and
exits. No allocation, device grant, libc, Aelthuvor or kernel source is required.

From this directory, with `../common/` present:

```sh
make SDK=/path/to/kernel-sdk
# In the Liberums root:
make run-libos LIBOS_ELF=/path/to/minimal/build/libos.elf
```

Default profile is preemptive. `make PROFILE=coop MIN_MINOR=1` builds an ABI 1.1
cooperative consumer; run that with the original `external` profile, not multi-external.
Configuration changes invalidate the runtime objects without requiring make clean.
Log output is limited to 128 characters per domain. ELF32 is guest code: load it
with QEMU/Multiboot, not as a native Linux executable.

[Lesson 1](../../../docs/libos-development/01-hello.md).
