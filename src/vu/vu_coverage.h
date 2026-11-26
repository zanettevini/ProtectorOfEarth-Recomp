// vu_coverage.h: how much of the VU1 and VU0 work ran as translated code
// (findings/21 s.27).
//
// The README board counts a VU microprogram as the port's own code only
// when no pair of it falls back to PS2Recomp's VU interpreter. The runtime
// counts, per unit, the calls into translated code and the pairs it handed
// to the interpreter (VU1Interpreter::nativeCoverage, patch 0048); report()
// prints what changed since its last call, as one line:
//   [vu-native] 10.0 s: VU1 1234567 translated calls, 0 pairs interpreted; VU0 ...
// followed, when pairs were interpreted, by the busiest PCs and why
// ("no table": no translated code for the code memory at all; "no pair":
// none for that pair).
#pragma once

class PS2Runtime;

namespace ben10::vu_coverage
{
    // Prints the coverage line for the last `seconds` (the caller's report
    // interval). Call from one thread only.
    void report(PS2Runtime &runtime, double seconds);
}
