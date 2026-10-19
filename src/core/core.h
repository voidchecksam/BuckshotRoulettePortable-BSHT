#pragma once

namespace core {

// Identifies the compiled simulation. Handy on device, where there is no other
// way to tell which build is actually running.
const char* BuildId();

}  // namespace core
