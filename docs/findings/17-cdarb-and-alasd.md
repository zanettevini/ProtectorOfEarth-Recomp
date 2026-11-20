# 17: the game's own IOP modules, CDARB and ALASD

## 1. The question

The boot now stalls with the IOP interpreter busy in disc reads
([15](15-first-boot.md) s.12). The readers are the game's own IOP modules,
`CDARB.IRX` and `ALASD.IRX` ([10](10-iop-modules.md)), which today run in
PS2Recomp's IOP interpreter (temporary debt T1). The project's priority is
to replace emulated pieces with native code (D2), so the question is: what
do these two modules do, what is their RPC protocol, and what would a
native replacement have to provide?

## 2. How it was studied

- New tool `tools/analyzer/irx_imports.py`: parses an IRX (ELF32, MIPS
  R3000, little-endian), lists its import stubs by library and index, with
  function names taken from the PS2SDK IOP headers
  (`DECLARE_IMPORT(N, name)`), lists its export table, and annotates an
  `llvm-objdump --triple=mipsel` listing so that `jal 0x2094` reads
  `jal sifcmd:17 (sceSifRegisterRpc)`.
- EE side: `tools/analyzer/rpc_calls.py` on the game's calls.
- Only addresses, sizes and roles are recorded here, never the modules'
  code.

## 3. Who is who

| Module | RPC server | Library it exports | Imports (summary) |
|---|---|---|---|
| `CDARB.IRX` ("CD_Arbiter") | SID `0x01212122` | `CDArb` (indices 4-8) | cdvdman (`sceCdInit`, `sceCdRead`, `sceCdSync`, `sceCdMmode`, `sceCdDiskReady`...), ioman (`open`/`read`/`lseek`/`close`), sifman (`sceSifSetDma`), sifcmd (RPC server + `sceSifSendCmd`), libsd (`sceSdVoiceTrans`), threads and semaphores |
| `ALASD.IRX` ("Argonaut_Audio_Driver") | SID `0x01212121` | none | libsd (init, parameters, voice/block transfers, effects, batch commands), timrman (a hardware timer), sifcmd (RPC server), **`CDArb:4` and `CDArb:6`** |

So ALASD is the game's sound engine running on the IOP (it drives the
SPU2 through libsd), and CDARB is a disc streaming service shared by the EE
(game data) and by ALASD (audio streams). ALASD links against CDARB: the
two cannot be replaced one without the other unless CDARB's exports keep
existing for ALASD.

## 4. CDARB's RPC protocol (SID `0x01212122`)

- Registered by its RPC thread (`.text+0x19C`): `sceSifInitRpc`,
  `sceSifSetCmdBuffer`, `sceSifAddCmdHandler` (a SIF command whose handler
  only increments a counter), `sceSifRegisterRpc(0x01212122, handler at
  .text+0x44, receive buffer)`, `sceSifRpcLoop`.
- The handler dispatches on the function number through an 8-entry jump
  table (`.rodata+0x0C`); anything else returns -1. The reply is 16 bytes
  (the result word first).

| fno | What it does (from the code) | Called by the EE? |
|---|---|---|
| 0 | init: stores the mode word; on first call (and in disc mode) `sceCdInit`, `sceCdMmode`, waits for `sceCdDiskReady`, sets up the request queue (`.text+0x73C`), creates and starts the worker thread; returns 0, or -2/-3/-4 for the three disc steps | yes, `FUN_0012c068` (sends 0x14 bytes) |
| 1 | host mode: open a file through ioman; disc mode: hand out the next handle number | no |
| 2 | queue a request: (id, 5 words of parameters, two more words) into the queue at `.bss`, flag 0 | yes, `FUN_0012c1f8` (sends 0x20 bytes) |
| 3 | wait for request `id` to finish (creates a semaphore on the request and waits) | no |
| 4 | close a host file and cancel its queued requests | no |
| 5 | host file size (`lseek` to the end) | no |
| 6 | store one word (purpose TBD) | no |
| 7 | queue a special request (type `0x80000001`) with two parameters | yes, `FUN_0012c2c8` (sends 0x10 bytes) |

- The worker thread (`.text+0x2D0`) waits on the queue's semaphore and
  serves one request at a time (`.text+0x884`): it reads with `sceCdRead`
  (disc mode) or ioman `read` (host mode), sends EE-bound data with
  `sceSifSetDma`, audio data to the SPU2 with `sceSdVoiceTrans`, and tells
  the EE that a request finished with `sceSifSendCmd` (the EE side
  registers the matching command handler in `FUN_0012c068`).
- The `CDArb` exports ALASD uses: index 4 = the init routine
  (`.text+0x300`), index 6 = queue a request with flag 8 (`.text+0x57C`,
  the audio path).

## 5. CDARB's request queue (read so far)

The queue insert (`.text+0xDD4`) fills a 0x2C-byte request and returns its
sequence number (the request id):

| Offset | Value |
|---|---|
| +0x00 | first argument: for function 2 the EE's first word; negative values are special requests (`0x80000001`, function 7, asks for `sceCdStatus` into +0x18) |
| +0x04, +0x08, +0x0C | words 1-3 of the parameters |
| +0x10 | request id (a global counter) |
| +0x14 | word 5 of the parameters (word 4 is not copied) |
| +0x18 | result, set by the worker |
| +0x1C | flags: 0 from the EE, 8 from ALASD (`CDArb:6`) |
| +0x20, +0x24 | completion: with flag 8, an IOP callback and its argument; otherwise two words handed back to the EE |
| +0x28 | semaphore of a waiter (function 3) |

The worker (`.text+0x884`) treats +0x08 as a count: positive and negative
values take different paths through a pool of buffers (`.text+0xF30`,
`.text+0x11EC`, `.text+0x1C38`, the last one doing the `sceSifSetDma` /
`sceSdVoiceTrans` transfers). When a request is done it either calls the
IOP callback (flag 8) or sends the EE a 0x18-byte SIF command, whose
command number is the one CDARB registered at init (`.bss+0x2370`), with
+0x0C = the result, +0x10/+0x14 = the two EE words. The meaning of words
1-5 (disc position, size, EE address...) is still to read in those three
routines.

## 6. What a native replacement needs

For CDARB (needed to get past the current stall):

1. An RPC server for SID `0x01212122` with functions 0, 2 and 7 (the ones
   the EE uses), whose reply layout matches the 16-byte reply.
2. The request semantics: the request struct (0x2C bytes) and the meaning
   of the parameters of functions 2 and 7 (disc sector, size, EE address,
   ...). The struct is in s.5; the meaning of
   the parameter words is still open (s.7).
3. Reads from `GAME.WAD` straight into EE RAM, then the completion SIF
   command to the EE, as the module does.
4. While ALASD still runs in the interpreter: `CDArb:4` and `CDArb:6` as
   native imports for it, or ALASD replaced at the same time.

For ALASD: a native sound engine (or a translation of its commands to a
host mixer) and a native SPU2 path. That is the larger piece.

PS2Recomp has no extension point for a game's own native IOP services: its
services (`MCSERV`, `DBCMAN`, `LIBSD`) are built in, and a module loaded
from `cdrom0:` is always run in the interpreter first. A small patch to
`ps2xIOP` is needed: let the game register native services by module
name, and prefer them over loading the IRX.

## 7. Open

- The meaning of parameter words 1-5 (s.5): the buffer routines
  `.text+0xF30`, `.text+0x11EC`, `.text+0x1C38`.
- Where the completion command number at `.bss+0x2370` comes from.
- The ALASD RPC protocol (handler at `.text+0x17F8`).
- Why the interpreted read loops (the s.12 stall of finding 15): a failing
  read retried forever, or just slow. Moot if CDARB becomes native.
