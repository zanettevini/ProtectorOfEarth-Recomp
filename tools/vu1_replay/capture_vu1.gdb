# capture_vu1.gdb: save VU1 workloads from a running ben10_poe (findings/20 s.2)
#
# WHAT IT IS FOR
#   Each time VIF1 resumes a VU1 microprogram (MSCNT), this saves what the
#   program starts from: VU1 micro memory (code), VU1 data memory and the
#   interpreter's register state (VU1State). tools/vu1_replay replays them
#   offline, to profile the VU1 code and to check that a change (to the
#   interpreter now, to native VU1 code later) gives bit-identical results.
#   At every resume seen so far the interpreter's pipelines were empty
#   (pipelinesPending() == 0, printed below), so these three files are the
#   whole starting state.
#
# THE FILES CONTAIN THE GAME'S VU CODE AND DATA: keep them in user/ (not
# versioned, AGENTS.md s.2). The output folder must exist.
#
# USAGE (game running, e.g. in the first level):
#   mkdir -p user/vu1_captures
#   gdb -p <pid of ben10_poe> -batch -x tools/vu1_replay/capture_vu1.gdb
# Run it from the repository root. It saves 40 workloads and detaches.
set pagination off
set $n = 0
break VU1Interpreter::resume if this->m_unit == 1
commands
silent
eval "dump binary memory user/vu1_captures/%03d_code.bin vuCode vuCode+16384", $n
eval "dump binary memory user/vu1_captures/%03d_data.bin vuData vuData+16384", $n
eval "dump binary value user/vu1_captures/%03d_state.bin this->m_state", $n
printf "captured %d pc=%u top=%u itop=%u pending=%d\n", $n, this->m_state.pc, top, itop, (int)this->pipelinesPending()
set $n = $n + 1
if $n >= 40
  detach
end
continue
end
continue
