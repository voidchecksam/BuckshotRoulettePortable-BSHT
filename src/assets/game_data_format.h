// The version of bsht-extract's game/ folder. The tool writes it into
// manifest.json and the port refuses any other. Raise it whenever a room, the
// sound bank or an export changes shape.
#pragma once

namespace assets {

inline constexpr int kGameDataFormat = 2;

}  // namespace assets
