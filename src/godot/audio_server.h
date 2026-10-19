// AudioServer, reduced to what the scripts touch: bus 0's volume
// (OptionsManager.ApplySettings_volume) and the buses' mute flags
// (OptionsManager.ApplySettings_music). The host reads the volume each frame
// as the mixer's master gain; OutputGain silences a player on a muted bus.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace godot {

class AudioServer {
public:
    // The game's bus layout (measured from Godot on v2.2); every bus sends
    // to Master.
    static constexpr std::array<std::string_view, 5> kBuses = {"Master", "music", "music_secondary",
                                                               "interactions", "music_resolve"};

    static AudioServer& get_singleton() {
        static AudioServer s;
        return s;
    }
    // The layout's own state: Master's volume (0.00282 dB in v2.2's
    // default_bus_layout), every bus unmuted.
    static constexpr float kMasterLayoutDb = 0.00282288f;
    void reset() {
        master_db_ = kMasterLayoutDb;
        mute_ = {};
    }
    void  set_bus_volume_db(int bus, float db) { if (bus == 0) master_db_ = db; }
    float get_bus_volume_db(int bus) const { return bus == 0 ? master_db_ : 0.0f; }

    // -1 for a name the layout does not have.
    int get_bus_index(std::string_view name) const {
        for (std::size_t i = 0; i < kBuses.size(); ++i) {
            if (kBuses[i] == name) return static_cast<int>(i);
        }
        return -1;
    }
    void set_bus_mute(int bus, bool mute) {
        if (bus >= 0 && bus < static_cast<int>(kBuses.size())) mute_[static_cast<std::size_t>(bus)] = mute;
    }
    bool is_bus_mute(int bus) const {
        return bus >= 0 && bus < static_cast<int>(kBuses.size()) && mute_[static_cast<std::size_t>(bus)];
    }
    // Bit i set: bus i muted.
    uint32_t mute_mask() const {
        uint32_t m = 0;
        for (std::size_t i = 0; i < mute_.size(); ++i) {
            if (mute_[i]) m |= 1u << i;
        }
        return m;
    }
    // The muted buses by name, for the log ("none" when every bus plays).
    std::string muted_text() const {
        std::string out;
        for (std::size_t i = 0; i < kBuses.size(); ++i) {
            if (!mute_[i]) continue;
            if (!out.empty()) out += ", ";
            out += kBuses[i];
        }
        return out.empty() ? "none" : out;
    }
    // Whether a player on `bus` is heard: its bus and Master both unmuted.
    bool is_bus_heard(std::string_view bus) const { return !is_bus_mute(0) && !is_bus_mute(get_bus_index(bus)); }

private:
    float master_db_ = kMasterLayoutDb;
    std::array<bool, kBuses.size()> mute_{};
};

}  // namespace godot
