// vitaGL's shader cache on the Vita, checked before vglInit reads it.
#pragma once

#include <string>

namespace platform {

// Removes every file in ux0:data/shader_cache/<titleId>/v1/{v,f} that
// CachedShaderIsWhole rejects (vitaGL compiles those again) and returns one
// line for the report.
std::string PruneShaderCache(const char* titleId);

}  // namespace platform
