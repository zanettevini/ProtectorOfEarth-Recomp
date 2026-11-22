# capture_vu0.gdb: save VU0 workloads from a running ben10_poe (findings/21 s.19)
#
# WHAT IT IS FOR
#   The game starts its VU0 microprograms (the skinning) with VCALLMS; the
#   runtime runs each call through PS2Runtime::executeVU0Microprogram, which
#   resets the VU0 instance, copies the EE's VU0 registers into it and calls
#   VU1Interpreter::execute. This breaks on that call and saves what the
#   program starts from: VU0 micro memory (code, 4 KB), VU0 data memory
#   (4 KB), the instance's VU1State and the call's start PC and ITOP.
#   tools/vu1_replay --vu0 replays them, interpreted or translated, and the
#   checksums tell whether the translated code computes the same thing.
#   VU0 runs many short programs per frame: one call in 23 is saved, so the
#   set covers more than one frame and more than one character.
#
# THE FILES CONTAIN THE GAME'S VU CODE AND DATA: keep them in user/ (not
# versioned, AGENTS.md s.2). The output folder must exist.
#
# USAGE (game running, in a level with characters on screen):
#   mkdir -p user/vu0_captures
#   gdb -p <pid of ben10_poe> -batch -x tools/vu1_replay/capture_vu0.gdb
# Run it from the repository root. It saves 60 workloads and detaches.
set pagination off
set $n = 0
set $calls = 0
break VU1Interpreter::execute if this->m_unit == 0
commands
silent
set $calls = $calls + 1
if $calls % 23 == 0
  eval "dump binary memory user/vu0_captures/%03d_code.bin vuCode vuCode+4096", $n
  eval "dump binary memory user/vu0_captures/%03d_data.bin vuData vuData+4096", $n
  eval "dump binary value user/vu0_captures/%03d_state.bin this->m_state", $n
  eval "dump binary value user/vu0_captures/%03d_args.bin startPC", $n
  eval "append binary value user/vu0_captures/%03d_args.bin itop", $n
  printf "captured %d pc=0x%x itop=%u\n", $n, startPC, itop
  set $n = $n + 1
end
# detach after the last one (a continue after detach would be an error)
if $n >= 60
  detach
else
  continue
end
end
continue
