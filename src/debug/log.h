// The debug log: what the operator did on the device, in the file that comes
// back.
//
// The overlay lets the device be asked a question the build was not compiled
// to answer, and the answer has to survive the trip home. So every overlay
// action is logged with the state around it: what was played, what it moved,
// what was forced visible and whether that changed anything. "played 'dealer
// use beer'" is worth little; "bound 17 of 22 tracks and moved 4 targets" is
// worth a round trip.
//
// A sink, not a file. debug/ links no platform, and on the Vita the report is
// owned by main_vita's Report struct, which flushes as it goes. The host hands
// in a callback, the log calls it, and the host decides where the bytes land
// and when.
#pragma once

#include "debug/overlay.h"
#include "debug/state.h"

#include <functional>
#include <string>

namespace debug {

// Receives one line, already formatted, without a trailing newline.
using LogSink = std::function<void(const std::string&)>;

// Describes an action the operator took, with enough surrounding state to be
// worth reading later. Called after the host has applied it, so what is logged
// is what happened rather than what was requested.
void LogAction(const LogSink& sink, const OverlayAction& action,
               const DebugState& s, bool applied);

// The overlay's position, logged when it opens and closes so a run's report
// says which pages were actually looked at.
void LogOverlayToggle(const LogSink& sink, bool nowVisible, Page page);

}  // namespace debug
