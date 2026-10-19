// Persistence for the translated scripts. One file per original save path
// (buckshotroulette_options_12.shell becomes options_12.sav), holding a flat
// key -> value map under the original's field names. Headless: game/ links no
// platform, and this uses only the C library's file calls, which newlib maps
// to sceIo on the Vita.
//
// Format: "BSAV", u32 version (1), u32 count, then per entry a type byte, a u32
// key length, the key bytes and the value (u8 bool, i64, f64, or u32 length +
// bytes). Little-endian. Anything that does not parse to the end loads as
// absent: the caller then uses its defaults and the next Save overwrites it, as
// the original does with a .shell it cannot read.
#ifndef BSHT_GAME_SAVE_STORE_H
#define BSHT_GAME_SAVE_STORE_H

#include <cstdint>
#include <map>
#include <string>
#include <variant>

namespace game {

using SaveValue = std::variant<bool, int64_t, double, std::string>;
using SaveMap = std::map<std::string, SaveValue>;

class SaveStore {
public:
    explicit SaveStore(std::string directory);

    // False when the save is absent or does not parse; `out` is then left empty.
    bool Load(const std::string& name, SaveMap& out) const;
    // Writes <name>.tmp, then replaces <name>.sav with it. False on any failure,
    // with the previous save untouched.
    bool Save(const std::string& name, const SaveMap& values) const;
    // True when no save remains.
    bool Clear(const std::string& name) const;
    bool Exists(const std::string& name) const;

    const std::string& Directory() const { return directory_; }

private:
    std::string PathOf(const std::string& name, const char* extension) const;

    std::string directory_;
};

}  // namespace game

#endif  // BSHT_GAME_SAVE_STORE_H
