# Liberums v1.0.0 — Educational Release

This is a teaching and research baseline, not a production security certification
or a general-purpose operating system. Kernel ABI remains **1.2** (26 scalar calls).
The source commit is recorded in the release manifest and the immutable tag.

Start with README.md / README.zh-CN.md, docs/architecture-guide/ and docs/libos-development/.
`make sdk libos-examples` exports public headers and builds independent Ring 3 programs.
No Aelthuvor checkout is needed for the kernel, SDK, examples or core tests.

Included mechanisms: Multiboot/i686, protected pages, independent CR3/stacks,
Capability owner/Epoch/Generation, bounded multi-ELF domains, IRQ0 arbitration,
visible RAM revocation, exclusively granted VGA/PS2 and user-space console policy.

Validation includes the original Phase 1–9 suites, 128-reuse stress, independent
outside consumers, active ACK/forced revocation/#PF and real simulated PS/2 input.
Different application APIs/allocators share the same kernel in the optional external
Aelthuvor compatibility matrix. Actual counts and limits: docs/release-validation.md.

Artifacts: source archive, canonical SDK archive, example sources, validation.json,
manifest.json, SHA256SUMS and the tested i686 kernel ELF. The ELF's toolchain/conditions
and SHA are recorded; source/SDK/example archives are deterministic for a fixed commit.
Do not infer cross-toolchain bit-identical kernel binaries from that archive guarantee.

Limits: single CPU, four live domains/eight boot modules, finite page/binding/event
quotas, exclusive devices, no general Spawn/POSIX/IPC/files/network/SMP/dynamic linking/GUI.
Cooperative mode lacks forced CPU progress; i686 has no full hardware NX/W^X;
revoked pointers are not transparently restored; transfer 22 routes by current active ID.
Renderer/PS2 simulation does not establish local GTK/physical-keyboard interaction.

License: GPL-3.0-or-later; see LICENSE and source SPDX notices. No Aelthuvor framework
is embedded; console teaching-source provenance is documented in its README.
Major development stops after Phase 10. Future work is maintenance, compatibility,
security fixes and teaching improvements, not an automatic Phase 11 or derivative OS.

Building assets is not publishing a GitHub Release. See docs/release-validation.md
for the delivery environment's publication status and the maintainer guide for publishing.
