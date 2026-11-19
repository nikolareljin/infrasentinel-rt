#pragma once

// Replays a recorded or generated event stream through the detector, with
// timer ticks placed exactly where a real timer would fire. Used by the
// simulator scenarios and by the benchmarks; the live engine feeds the same
// detector from the network instead.
//
// Input, one event per line ('#' starts a comment):
//   node <name> <period_ms> [miss_periods]
//   msg  <t_ns> <name> <boot_hex> <seq>
//   end  <t_ns>
// Times are monotonic nanoseconds and must not decrease.
//
// Output, one JSON object per line: every alert, then at `end` the health of
// each node.

#include <iosfwd>

namespace isrt {

// Returns 0 on success, 1 on a malformed input line or a detector that does
// not advance (reported on `err`).
int replay(std::istream& in, std::ostream& out, std::ostream& err);

}  // namespace isrt
