// The census as text, for the file that comes back from the device.
//
// Formatting is separate from gathering and from drawing so all three can be
// tested apart. This half links nothing platform-specific: it turns a
// DebugState into lines.
//
// No %zu: newlib on arm-vita-eabi does not support it. It prints a literal
// "zu" and desynchronises every following vararg, which crashes the device.
// Sizes are cast to `unsigned long` and printed with %lu.
#pragma once

#include "debug/state.h"

#include <string>

namespace debug {

// Every section, in the order a reader wants them: what shipped, what bound,
// what is on screen. A section that would be empty says so rather than being
// left out: an absent section reads as "not measured".
std::string FormatCensus(const DebugState& s);

// The live half, cheap enough to append periodically during a run.
std::string FormatFrame(const FrameCensus& f);

}  // namespace debug
