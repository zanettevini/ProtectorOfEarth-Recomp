# 06: conflicts between the master prompt and what other ports do

## 1. The question

The master prompt forbids a CPU interpreter, a dynarec and a hidden emulator
(s.3, s.10). The reference ports ([02](02-reference-projects.md)) use an
interpreter in three places. What to do about each one?

## 2. Conflict A: the IOP

- **What the others do**: PS2Recomp's `ps2xIOP` and Gitaroo Man run the
  original IRX modules on an emulated R3000A.
- **Why it conflicts**: the IOP is a PS2 CPU. Running its code by
  interpretation is a "PS2 CPU interpreter".
- **Compatible path**: BT2 Recomp's. Do not run IRX; implement in native C++
  the **services** the game asks the IOP for over RPC (disc reads, file
  system, pad, memory card, sound). The master prompt allows native
  implementations of subsystems (s.3).
- **Proposal**: adopt it. Cost: we have to find every RPC the game uses
  (phase 1).

## 3. Conflict B: EE code generated at run time

- **What the others do**: Gitaroo Man falls back to an EE interpreter.
- **Compatible path**: first measure whether Ben 10 does this. If it does,
  it is usually known code (an overlay on the disc, or a block copied to
  another address). Overlays on the disc can be recompiled statically at
  build time, as BT2 does with its overlay. Code that is truly generated on
  the fly is a documented blocker (master prompt s.3).
- **Proposal**: measure first; recompile overlays statically.

## 4. Conflict C: VU microprograms

- **What the others do**: Gitaroo Man recompiles the VU1 microprogram to C++
  and keeps a VU1 interpreter as a fallback and as a test oracle
  (side-by-side comparison).
- **Why it partly conflicts**: recompilation is exactly what the master
  prompt asks for (phase 3). What conflicts is keeping the interpreter as a
  fallback in the executable.
- **Proposal**: microprograms translated statically at build time. A VU
  interpreter **only in the test target** (`tests/`), for comparison, never
  in the game executable. If the game uploads a microprogram that was not
  translated, the runtime stops with an error and the program's hash (no
  silent fallback).

## 5. The same reasoning for the EE

To validate EE instructions (phase 2) we need a reference. Options: PCSX2
captures, or a minimal interpreter **only in tests**. Same rule: never in
the final executable.

## 6. Decision

The owner asked for the easiest path for now. Result
([DECISIONS](../DECISIONS.md) D2, D3):

- A (IOP): `ps2xIOP` running the IRX, **temporary**. Back to native services
  before phase 8.
- B (run-time code): **does not exist in this game**, as far as can be told
  ([09](09-executable.md) s.8). Nothing to do.
- C (VU): any available implementation for the first frame, **temporary**;
  static translation is feasible ([11](11-vu-microprograms.md)).

Every temporary use of interpretation is recorded as a debt in
[KNOWN_ISSUES](../KNOWN_ISSUES.md) and shows up in the log.
