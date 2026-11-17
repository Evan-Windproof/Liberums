# Phase 10 release evidence

Records are produced by actual execution in the i686/QEMU TCG, one CPU/32 MiB environment.

- `validation.json` records every release command and its real exit status, source inputs and frozen kernel SHA.
- `phase10-results.json` / `phase10-compat-results.json`: 11 teaching/consumer and 4 external mixed-LibOS guests.
- `consumer-build.log` / `consumer-dependencies.json`: outside build commands, sources, dependencies and link maps.
- `phase10-*-gdb.log` / `*-serial.log`: actual CPL3/CR3/PTE/ownership/IRQ/reclamation observations.
- `console.png`: simulated PS/2 input rendered by QEMU, not a cloud GTK window.
- `baseline.log` and command logs: old Phase 1–9 and final clean regression/standalone suites.
- `kit.log`, `links.log`, `provenance.json`: export/tool/link/license/source checks and external checkout state.
- `github-api.log`: the API access failure; release files or a Git tag do not establish a published GitHub Release.

Native simulation, QEMU/GDB, rendering and manual local interaction are distinct evidence levels.
Text logs normalize LF/trailing blank space only. Generated ELF/objects/SDK copies remain outside Git.
Historical reports keep their original locations. See [release validation](../release-validation.md).

## Recorded run

The 2026-11-17 release run completed **17/17 commands with exit status 0**.
Its frozen kernel SHA256 is
`360539a3d9e9106f792dec5cd73d89ec348243072097882b4607c780d961c5a1`.
The final release commit is resolved after committing these records; the packaged
manifest names that commit and verifies all tested source-input hashes against it.

| Command log | Check |
|---|---|
| [00](commands/00.log), [01](commands/01.log), [02](commands/02.log) | Clean, build, original full regression |
| [03](commands/03.log), [04](commands/04.log) | Phase 8 and Phase 9 core (including 128 reuse rounds) |
| [05](commands/05.log), [09](commands/09.log) | 11 independent teaching/consumer guests and 4 mixed external guests |
| [06](commands/06.log), [07](commands/07.log) | SDK/tool/project checks and local Markdown destinations |
| [08](commands/08.log) | 21-guest Phase 9 external RAM E2E |
| [10](commands/10.log), [11](commands/11.log), [12](commands/12.log) | Aelthuvor Phase 4 SDK, memory and Console |
| [13](commands/13.log), [14](commands/14.log), [15](commands/15.log), [16](commands/16.log) | Historical external compatibility |

[validation.json](validation.json) retains the original `build/release-checks/`
paths; the byte-identical command logs are archived here under `commands/`.
[provenance.json](provenance.json) records their hashes, unchanged canonical
headers/license, source checkout identities and the limits of each evidence level.
[Consumer dependencies](consumer-dependencies.json) retain actual temporary paths
as observations; these paths are not required by the standalone projects.
The SDK manifest here describes the tested working tree; the release SDK manifest
uses the fixed final Git revision. Their public-header bytes are identical.
