// source: scripts/LeaderboardManager.gd (258 lines)
// PORT deviations:
//   - There is no Steam. The leaderboard is local: UploadScore appends the run
//     to game::LocalBoard (SaveStore entry "board", `store` set by the host on
//     a player's boot), and DownloadEntries answers from it. Steam's callbacks
//     keep their names and arrive one deferred call later, as Steam's arrive
//     on a later frame.
//   - A result row is a struct (BoardResult). Its steam_id is the run's name;
//     the "user" of GLOBAL_AROUND_USER and getSteamID() is the waiver name, or
//     when none is signed yet, the name on the latest uploaded run.
//   - display_instances_act holds Label3D (both exported arrays are Label3D
//     nodes; display_instances_alt is typed Node3D in the .gd).
//   - details_max (Steam.setLeaderboardDetailsMax) and the prints of
//     _on_leaderboard_scores_downloaded have no local meaning.
#pragma once

#include "godot/animation_player.h"
#include "godot/control.h"
#include "godot/node.h"
#include "godot/physics.h"
#include "godot/script.h"
#include "scripts/forward.h"
#include "seq/task.h"

#include <cstdint>
#include <string>
#include <vector>

namespace game {
class SaveStore;
}

namespace scripts {

// One row of a Steam leaderboard download: global_rank, steam_id, score, details.
struct BoardResult {
    int global_rank = 0;
    std::string steam_id;
    int64_t score = 0;
    std::vector<int64_t> details;
};

class Board : public godot::Script {
public:
    static constexpr const char* kClassName = "Board";
    const char* get_class_name() const override { return kClassName; }

    Achievement* ach = nullptr;
    CRT* crt = nullptr;
    SaveFileManager* save = nullptr;
    std::vector<godot::Label3D*> display_instances;
    std::vector<godot::Node3D*> display_instances_alt;
    std::vector<godot::Label3D*> display_instances_act;
    godot::Node3D* display_overview = nullptr;
    godot::Label3D* text_globalpos = nullptr;
    godot::Node3D* nocon = nullptr;
    godot::Node3D* lock = nullptr;
    int leaderboard_handle = 0;
    std::vector<BoardResult> active_result_array;
    int active_entry_count = 0;

    const game::SaveStore* store = nullptr;   // PORT: the local board's save; null on diagnostics

    void _ready() override;                                          // gd: L19-L26
    bool has_process() const override { return true; }
    void _process(double delta) override;                            // gd: L28-L29
    bool call(const std::string& method, const std::vector<godot::Variant>& args) override;
    void PassLeaderboard(int range1, int range2, const std::string& alias);   // gd: L31-L35

    bool initialsetup = false;
    std::string active_alias;
    int active_range1 = 0;
    int active_range2 = 0;
    void SetupLeaderboard();                                         // gd: L41-L49
    void Timeout(bool s);                                            // gd: L51-L57
    void ClearDisplay();                                             // gd: L59-L65
    void TurnOffDisplay();                                           // gd: L67-L81
    void UpdateDisplay();                                            // gd: L83-L146

    godot::Label3D* stat_shotsFired = nullptr;
    godot::Label3D* stat_shellsEjected = nullptr;
    godot::Label3D* stat_doorsKicked = nullptr;
    godot::Label3D* stat_cigSmoked = nullptr;
    godot::Label3D* stat_roundsBeat = nullptr;
    godot::Label3D* stat_mlDrank = nullptr;
    godot::Label3D* stat_totalCash = nullptr;
    void UpdateStats();                                              // gd: L156-L166
    void GetLeaderboard();                                           // gd: L168-L169
    // PORT: omitted UploadScore_Deprecated (never called; v1.2's initial x rounds upload), gd: L171-L174
    void UploadScore(int rounds_beat, int visible_score, int initial_score);   // gd: L176-L181
    // PORT: omitted val2compressed (the local board ranks with it: game::CompressedScore), gd: L236-L241
    // PORT: omitted _on_leaderboard_score_uploaded (only prints; nothing local to report), gd: L183-L186
    void _on_leaderboard_find_result(int handle, int found);         // gd: L188-L194

    bool checking_friends = false;
    bool checking_overview = false;
    void DownloadEntries(int range1, int range2, const std::string& alias);   // gd: L198-L212

    double t = 0;
    double max = 2;
    bool checking = false;
    void CheckTimeout();                                             // gd: L217-L221

    void _on_leaderboard_scores_downloaded(const std::string& message, int handle);   // gd: L225-L234

private:
    // PORT: the local "user" (getSteamID): the waiver name, else the latest run's.
    std::string LocalUser() const;
    // PORT: what a download answered, handed to the deferred callback.
    std::vector<BoardResult> downloaded_;
    int downloaded_count_ = 0;
};

}  // namespace scripts
