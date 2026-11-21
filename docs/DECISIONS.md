# Architecture decisions

Each decision: what, why, alternatives, and status
(**accepted** / **proposed** / **rejected**).

## D1. Recompiler base: PS2Recomp

- **Status**: accepted.
- **Why**: it covers ELF parsing, R5900 decoding, MMI, VU0 macro mode, C++
  generation, syscalls and overrides; it has already taken three games to a
  playable or nearly playable state.
- **Alternative**: our own recompiler from scratch. Rejected for cost.
- **Consequence**: the project is GPL-3.0.
- See [findings/01](findings/01-ps2recomp.md).

## D2. IOP: `ps2xIOP` now, native services later

- **Status**: accepted as **temporary**. The maintainer asked for "the
  easiest path for now".
- **Now**: use PS2Recomp's `ps2xIOP` as it comes (it runs the disc's IRX
  modules). It is the fastest way to a boot.
- **Later (before phase 8)**: replace it with native services, as master
  prompt s.3 requires. The change is small: only 8 modules are loaded, 5 of
  them standard Sony modules ([findings/10](findings/10-iop-modules.md)).
- **Recorded debt**: T1 in [KNOWN_ISSUES](KNOWN_ISSUES.md).
- See [findings/06](findings/06-master-prompt-conflicts.md) s.2.

## D3. Interpreters

- **Status**: accepted as **temporary** ("the easiest path for now").
- **EE**: no interpreter needed. All EE code is static
  ([findings/09](findings/09-executable.md) s.8); an unresolved call stops
  the run with an error.
- **VU**: for the first frame, use the VU0/VU1 implementation already in the
  PS2Recomp runtime, which interprets
  ([findings/13](findings/13-first-recompilation.md) s.5). Static
  translation comes later and already has the closed list of microprograms
  ([findings/11](findings/11-vu-microprograms.md)). Debt T2.
- **Rule that does not change**: every use of an interpreter writes a
  warning to the log at start-up, so it never goes unnoticed (master prompt:
  "do not silently fall back to interpretation").
- See [findings/06](findings/06-master-prompt-conflicts.md) s.4-5.

## D4. GS in Vulkan via paraLLEl-GS

- **Status**: proposed.
- **Why**: Vulkan, LGPL-3.0, already integrated in a Windows port.
- **Risk**: AMD drivers. See [KNOWN_ISSUES](KNOWN_ISSUES.md).
- See [findings/05](findings/05-vulkan-renderer.md).

## D5. Target version: SLUS-21661 (NTSC-U), revision 1.01

- **Status**: accepted (it is the available disc; confirmed by `SYSTEM.CNF`).
- See [findings/03](findings/03-the-game.md).

## D6. First platform: Windows x64, MSVC, CMake, SDL3, GoogleTest

- **Status**: accepted (master prompt s.5).
- Linux too; nothing Windows-only outside platform code.

## D7. Documentation format of Mind over Recomp, in English

- **Status**: accepted (maintainer's request).
- Everything in the repository is English: docs, findings, comments, script
  messages, commits (AGENTS.md s.0). The early study notes were first
  written in Portuguese and translated when the repository was created.

## D8. Pin PS2Recomp at commit `e563698`

- **Status**: accepted.
- **Why**: the project moves fast; the results of
  [findings/13](findings/13-first-recompilation.md) and
  [findings/14](findings/14-repository-build-and-tests.md) hold for this
  commit. Updating is a task of its own, with a new recompilation and
  comparison.

## D9. Repository organised like Mind over Recomp

- **Status**: accepted (maintainer's request).
- **What**: `ben10_poe.toml` (commented recompiler config), `src/` (port
  code), `generated/` (local), `patches/ps2recomp/`, `tools/`,
  `docs/findings/`, `tools/native_progress.py` + the README board.
- **Difference from master prompt s.5**: the structure suggested there
  (`recomp/`, `runtime/ee/`...) assumes our own recompiler. With PS2Recomp
  as a submodule, the recompiler and runtime live in `thirdparty/PS2Recomp`,
  and what is ours goes to `src/` (by topic, once it exists) and `patches/`.
  The tracking files required by master prompt s.9 stay in `docs/`.

## D10. PS2Recomp patches applied by CMake

- **Status**: accepted.
- **Why**: the submodule stays at the pinned commit (D8); fixes live in
  `patches/ps2recomp/NNNN-*.patch`, each with a header; configure applies
  them in order ([findings/14](findings/14-repository-build-and-tests.md)
  s.2). Nobody applies anything by hand. Since findings/20, a stamp with
  the patch set's hash decides: when the set changes, configure resets the
  submodule's files and reapplies every patch (checking each patch alone
  failed when a new patch touched the same lines as an older one).

## D11. Sony library functions are bound to runtime handlers by address

- **Status**: accepted.
- **Why**: the executable is stripped, and the PS2Recomp runtime answers the
  Sony libraries (libsif, libcdvd...) by name instead of emulating the IOP at
  hardware level. Each function is identified with evidence and bound with
  `"name@0xADDRESS"` in `ben10_poe.toml`, a whole library family at a time
  ([findings/15](findings/15-first-boot.md)).

## D12. The game's IOP modules become native services, CDARB first

- **Status**: accepted (maintainer: native code is always the priority).
- **What**: `CDARB.IRX` and `ALASD.IRX` stop running in the IOP interpreter
  and are replaced by native services in `src/iop/`
  ([findings/17](findings/17-cdarb-and-alasd.md)). CDARB first, because
  the boot is stuck in its disc reads; while ALASD still runs interpreted,
  the two `CDArb` exports it imports are provided natively.
- **How**: a `ps2xIOP` patch adds an extension point so the game can
  register native services by module name, preferred over loading the
  IRX. The services reproduce each module's RPC protocol, read from the
  IRX with `tools/analyzer/irx_imports.py`.
- **Instead of**: fixing the interpreter's disc-read path, which would only
  make the temporary debt T1 run better.

## D13. Screenshots of the port may be published, credited

- **Status**: accepted (maintainer approval, with the condition that the
  credit is correct and says the content is not the maintainer's work).
- **What**: a few screenshots captured from the port go in
  `docs/images/screenshots/` and the README, to document milestones and
  findings.
- **How**: each is listed in `docs/images/screenshots/README.md`, which
  credits the game's developer, publisher and trademark owner and says the
  content is theirs, not the project's; `CREDITS.md` says the same.
- **Instead of**: describing the screen in words only. Everything else in
  the "no game data" rule stays (AGENTS.md s.2).

## D14. Native VU1: the .vutext programs translated to C++, the interpreter as the reference

- **Status**: proposed (design; nothing translated yet).
- **What**: the game's VU1 microprograms (closed list in `.vutext`,
  [findings/11](findings/11-vu-microprograms.md); the first level runs
  group F, entries #13-#20, [findings/20](findings/20-vu1-census-and-gs-thread.md)
  s.1) are translated to C++ at build time by our own translator, like the
  EE code, and run instead of PS2Recomp's VU1 interpreter (debt T2).
- **How**:
  - Identification: when VIF1 starts or resumes a program (MSCAL/MSCNT),
    the runtime checks that the VU1 code memory holds a translated group
    (by content hash of the uploaded chunks, which come from known `.vutext`
    entries); otherwise the interpreter runs and the log says so (D3).
  - The translated code works on the interpreter's own state (registers,
    pipeline queues, cycle counter, XGKICK), so switching between native
    and interpreted code at a pair boundary is always possible.
  - Exactness first: the first translation keeps the interpreter's timing
    model (FMAC/FDIV/EFU latencies, delayed flags, stalls) with every
    decision the interpreter takes per pair made at translation time
    (decode, hazard sets, latencies, branch delay slots). Then, block by
    block, the translator computes the pipeline timing statically where
    the state at block entry is known (it is empty at every MSCNT resume
    seen, findings/20 s.2) and drops the queues where no result is
    observed early.
  - Reference: `tools/vu1_replay` replays workloads captured in the game;
    a translation is accepted only when every workload gives bit-identical
    results with the interpreter (registers, flags, cycle count, VU memory,
    GS memory). The interpreter stays in the source as that reference.
- **Instead of**: a run-time JIT (the master prompt forbids generating code
  at run time), or a translation that ignores VU timing (programs read
  flags and Q/P with fixed delays; results would differ from the
  interpreter and from the hardware).
