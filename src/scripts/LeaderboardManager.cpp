#include "scripts/LeaderboardManager.h"

#include "game/local_board.h"
#include "game/save_store.h"
#include "godot/array_ops.h"
#include "godot/scene_tree.h"
#include "scripts/AchievementManager.h"
#include "scripts/CrtManager.h"
#include "scripts/GlobalVariables.h"
#include "scripts/PartitionBranch.h"
#include "scripts/RoundManager.h"
#include "scripts/SaveFileManager.h"
#include "scripts/resources.h"

#include <algorithm>
#include <cstdio>
#include <variant>

namespace scripts {

namespace {

// `node.material_override.albedo_color = c` on a node the .gd types Node3D:
// GDScript reaches the GeometryInstance3D property dynamically.
void OverrideAlbedo(godot::Node3D* node, const godot::Color& c) {
    if (auto* g = godot::cast_to<godot::GeometryInstance3D>(node)) g->set_override_albedo(c);
}

constexpr int kLocalHandle = 1;   // PORT: the one local board's "handle"

godot::Variant IntArg(int v) {
    godot::Variant a;
    a.kind = assets::ValueKind::Int;
    a.f[0] = static_cast<float>(v);
    return a;
}

godot::Variant StrArg(const std::string& v) {
    godot::Variant a;
    a.kind = assets::ValueKind::Str;
    a.s = v;
    return a;
}

// str(x) of a stats Dictionary value: ints as ints, floats as Variant's rtos.
std::string StatText(const game::SaveMap& m, const char* key) {
    const auto it = m.find(key);
    if (it == m.end()) return "0";
    if (const auto* i = std::get_if<int64_t>(&it->second)) return std::to_string(*i);
    if (const auto* d = std::get_if<double>(&it->second)) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.14g", *d);
        return buf;
    }
    return "0";
}

BoardResult ResultOf(const game::BoardRun& run, int rank) {
    BoardResult r;
    r.global_rank = rank;
    r.steam_id = run.name;
    r.score = game::RankScore(run);
    r.details = {run.initial, run.beaten};
    return r;
}

}  // namespace

// gd: L19-L26. The Steam signal connects are the deferred callbacks below.
void Board::_ready() {
    display_instances_act = display_instances;
    ClearDisplay();
    if (!GlobalVariables::using_steam) return;
    GetLeaderboard();
}

// gd: L28-L29
void Board::_process(double /*delta*/) {
    CheckTimeout();
}

// The Steam callbacks, delivered as deferred calls.
bool Board::call(const std::string& method, const std::vector<godot::Variant>& args) {
    if (method == "_on_leaderboard_find_result" && args.size() == 2) {
        _on_leaderboard_find_result(args[0].AsInt(), args[1].AsInt());
        return true;
    }
    if (method == "_on_leaderboard_scores_downloaded" && args.size() == 2) {
        _on_leaderboard_scores_downloaded(args[0].s, args[1].AsInt());
        return true;
    }
    return false;
}

// gd: L31-L35
void Board::PassLeaderboard(int range1, int range2, const std::string& alias) {
    active_range1 = range1;
    active_range2 = range2;
    active_alias = alias;
    SetupLeaderboard();
}

// gd: L41-L49
void Board::SetupLeaderboard() {
    if (!initialsetup) {
        GetLeaderboard();
        // gd: Steam.setLeaderboardDetailsMax(2); no local meaning.
        initialsetup = true;
        return;
    }

    DownloadEntries(active_range1, active_range2, active_alias);
    Timeout(true);
}

// gd: L51-L57
void Board::Timeout(bool s) {
    if (s) {
        t = 0;
        checking = true;
    } else {
        t = 0;
        checking = false;
    }
}

// gd: L59-L65
void Board::ClearDisplay() {
    display_overview->set_visible(false);
    text_globalpos->set_text("");
    for (godot::Label3D* disp : display_instances_act) {
        auto* score = godot::cast_to<godot::Label3D>(disp->get_child(0));
        disp->set_text("");
        score->set_text("");
    }
}

// gd: L67-L81
void Board::TurnOffDisplay() {
    crt->speaker_bootuploop->stop();
    crt->speaker_shutdown->play();
    nocon->set_visible(false);
    display_overview->set_visible(false);
    text_globalpos->set_text("");
    for (godot::Label3D* disp : display_instances_act) {
        auto* score = godot::cast_to<godot::Label3D>(disp->get_child(0));
        disp->set_text("");
        score->set_text("");
    }
    for (godot::Node3D* i : crt->array_bootup) i->set_visible(false);
    for (godot::Node3D* i : crt->array_bootuplogo) i->set_visible(false);
    for (PartitionBranch* i : crt->array_partbranch) i->ResetPartition();
    for (godot::Node3D* i : crt->array_stats) i->set_visible(false);
    crt->anim_iconfade->play("RESET");
}

// gd: L83-L146
void Board::UpdateDisplay() {
    if (crt->has_exited) return;
    bool fs = false;
    Timeout(false);
    OverrideAlbedo(nocon, godot::Color{1, 1, 1, 0});
    OverrideAlbedo(lock, godot::Color{1, 1, 1, 0});
    display_instances_act = display_instances;
    int temp_rank = 0;

    if (checking_friends) {
        for (BoardResult& result : active_result_array) {
            temp_rank += 1;
            result.global_rank = temp_rank;
        }
    }

    if (checking_overview) {
        const std::string id = LocalUser();   // gd: Steam.getSteamID()
        bool has_current = false;              // gd: pos_current == null
        int pos_current = 0;
        const int pos_max = active_entry_count;
        display_overview->set_visible(true);
        for (const BoardResult& result : active_result_array) {
            if (result.steam_id == id) {
                pos_current = result.global_rank;
                has_current = true;
                break;
            }
        }
        if (!has_current) {
            OverrideAlbedo(lock, godot::Color{1, 1, 1, 1});
            display_overview->set_visible(false);
        }
        text_globalpos->set_text("(" + (has_current ? godot::str(pos_current) : std::string("<null>")) + "/" +
                                 godot::str(pos_max) + ")");
        display_instances_act.clear();
        for (godot::Node3D* n : display_instances_alt) display_instances_act.push_back(godot::cast_to<godot::Label3D>(n));
    }

    for (std::size_t i = 0; i < display_instances_act.size(); ++i) {
        if (i >= active_result_array.size()) continue;

        std::vector<int64_t> details = active_result_array[i].details;

        if (details.size() != 2) details = {0, 0};

        const int64_t init = details[0];
        const int64_t beaten = details[1];
        int64_t sc = init;
        for (int64_t s = 0; s < beaten - 1; ++s) sc *= 2;
        const int64_t active_score = sc;

        if (!fs) {
            ach->UnlockAchievement("ach16");
            crt->speaker_navbeep->set_pitch_scale(.5f);
            crt->speaker_navbeep->play();
            fs = true;
        }
        auto* scoretext = godot::cast_to<godot::Label3D>(display_instances_act[i]->get_child(0));
        const std::string playername = active_result_array[i].steam_id;   // gd: Steam.getFriendPersonaName(id)
        const int active_rank = active_result_array[i].global_rank;
        std::string mainstring = "#" + godot::str(active_rank) + " " + playername;
        const std::size_t char_limit = 18;

        if (mainstring.size() > char_limit) mainstring = mainstring.substr(0, char_limit - 1) + "-";
        display_instances_act[i]->set_text(mainstring);
        scoretext->set_text(std::to_string(active_score));
    }
}

// gd: L156-L166
void Board::UpdateStats() {
    game::SaveMap temp_data;
    if (store && store->Load(SaveFileManager::savePath_stats, temp_data)) {
        stat_shotsFired->set_text(StatText(temp_data, "shots_fired"));
        stat_shellsEjected->set_text(StatText(temp_data, "shells_ejected"));
        stat_doorsKicked->set_text(StatText(temp_data, "doors_kicked"));
        stat_cigSmoked->set_text(StatText(temp_data, "cigs_smoked"));
        stat_roundsBeat->set_text(StatText(temp_data, "rounds_beat"));
        stat_mlDrank->set_text(StatText(temp_data, "ml_drank"));
        stat_totalCash->set_text(StatText(temp_data, "total_cash"));
    }
}

// gd: L168-L169. Steam.findLeaderboard("double_or_nothing"): the local board
// is always found, one deferred call later.
void Board::GetLeaderboard() {
    get_tree()->call_deferred(get_node_owner(), "_on_leaderboard_find_result", {IntArg(kLocalHandle), IntArg(1)});
}

// gd: L176-L181. Steam.uploadLeaderboardScore(val2compressed(initial doubled per
// round after the first), keepBest, [initial, rounds]) -> the local board, which
// ranks by that key (game::RankScore). The name Steam shows is the account's;
// the local board's is the waiver name.
void Board::UploadScore(int rounds_beat, int /*visible_score*/, int initial_score) {
    game::BoardRun run;
    run.name = save && save->roundManager && save->roundManager->playerData
                   ? save->roundManager->playerData->playername
                   : std::string();
    run.initial = initial_score;
    run.beaten = rounds_beat;
    game::LocalBoard board;
    board.Append(store, run);
}

// gd: L188-L194
void Board::_on_leaderboard_find_result(int handle, int found) {
    if (found == 1) {
        leaderboard_handle = handle;
        // gd: print("Leaderboard handle found: %s" % leaderboard_handle)
        SetupLeaderboard();
    } else {
        // gd: print("No handle was found")
    }
}

// gd: L198-L212. The three Steam requests, answered from the local runs.
void Board::DownloadEntries(int range1, int range2, const std::string& alias) {
    if (!GlobalVariables::using_steam) return;
    game::LocalBoard local;
    local.Load(store);
    const std::vector<game::BoardRun>& runs = local.Runs();
    std::vector<BoardResult> result;
    auto ranks = [&](int from, int to) {
        for (int rank = std::max(from, 1); rank <= to && rank <= static_cast<int>(runs.size()); ++rank) {
            result.push_back(ResultOf(runs[static_cast<std::size_t>(rank - 1)], rank));
        }
    };
    bool answered = true;
    if (alias == "top") {
        checking_friends = false;
        checking_overview = false;
        ranks(range1, range2);   // LEADERBOARD_DATA_REQUEST_GLOBAL
    } else if (alias == "overview") {
        checking_friends = false;
        checking_overview = true;
        // LEADERBOARD_DATA_REQUEST_GLOBAL_AROUND_USER, -4 .. 4: none without the user.
        const std::string user = LocalUser();
        for (std::size_t i = 0; i < runs.size(); ++i) {
            if (runs[i].name == user) {
                const int at = static_cast<int>(i) + 1;
                ranks(at - 4, at + 4);
                break;
            }
        }
    } else if (alias == "friends") {
        checking_friends = true;
        checking_overview = false;
        ranks(range1, range2);   // PORT: a Vita has no friends list: the same runs
    } else {
        answered = false;        // gd: no request is made for any other alias
    }
    if (!answered) return;
    downloaded_ = std::move(result);
    downloaded_count_ = static_cast<int>(runs.size());   // Steam.getLeaderboardEntryCount
    get_tree()->call_deferred(get_node_owner(), "_on_leaderboard_scores_downloaded",
                              {StrArg("local"), IntArg(leaderboard_handle)});
}

// gd: L217-L221
void Board::CheckTimeout() {
    if (checking) {
        t += get_process_delta_time();
        if (t > max) OverrideAlbedo(nocon, godot::Color{1, 1, 1, 1});
    }
}

// gd: L225-L234
void Board::_on_leaderboard_scores_downloaded(const std::string& /*message*/, int handle) {
    // gd: print("scores downloaded message: %s" % message)
    // gd: `var leaderboard_handle: int = this_leaderboard_handle` declares a
    // LOCAL that shadows the member and is never read.
    (void)handle;

    active_result_array = downloaded_;
    active_entry_count = downloaded_count_;

    // gd: the print of each result
    UpdateDisplay();
}

std::string Board::LocalUser() const {
    if (save && save->roundManager && save->roundManager->playerData &&
        !save->roundManager->playerData->playername.empty()) {
        return game::BoardName(save->roundManager->playerData->playername);
    }
    game::LocalBoard local;
    local.Load(store);
    return local.Latest();
}

}  // namespace scripts
