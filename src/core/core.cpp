// Translation unit for the bsht_core static library.
//
// core/ is almost entirely headers and small .cpp files added by later tasks.
// This file gives the library a TU and carries the build identity, which is
// useful when a Vita build reports which simulation it is running.
#include "core/core.h"

namespace core {

const char* BuildId() { return "bsht-core " __DATE__ " " __TIME__; }

}  // namespace core
