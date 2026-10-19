// The Double or Nothing leaderboard, kept locally. The original uploads each
// cashed-out run to Steam (LeaderboardManager.gd); this port has no Steam, so
// the run is appended to SaveStore entry "board" and the pages are drawn from
// the local list. Headless: game/ links no platform.
#ifndef BSHT_GAME_LOCAL_BOARD_H
#define BSHT_GAME_LOCAL_BOARD_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace game {

class SaveStore;

struct BoardRun {
    std::string name;      // the waiver name
    int64_t initial = 0;   // details[0]: the run's first score
    int64_t beaten = 0;    // details[1]: rounds beaten
};

// The name a run is listed under: the waiver name without v2.2's leading space
// (SignatureManager stores " " + name) or trailing spaces.
std::string BoardName(std::string_view waiverName);

// v2.2's val2compressed (LeaderboardManager.gd): 17 bits of mantissa, so any
// score fits Steam's 32-bit board. GDScript does it in floats; so does this.
int64_t CompressedScore(int64_t val);
// What Steam ranks v2.2's uploads by: CompressedScore(ShownScore(run)).
int64_t RankScore(const BoardRun& run);
// What the board SHOWS: the initial score doubled once per round after the first.
int64_t ShownScore(const BoardRun& run);

class LocalBoard {
public:
    static constexpr const char* kEntry = "board";
    static constexpr std::size_t kMaxRuns = 100;

    // Reads the runs, best first. A missing or unreadable entry is an empty board.
    void Load(const SaveStore* store);
    // Loads, adds the run, ranks, keeps the best kMaxRuns and saves. False when
    // there is no store or the save fails.
    bool Append(const SaveStore* store, const BoardRun& run);
    const std::vector<BoardRun>& Runs() const { return runs_; }
    // The name on the run appended last ("" on an empty board).
    const std::string& Latest() const { return latest_; }

private:
    std::vector<BoardRun> runs_;
    std::string latest_;
};

}  // namespace game

#endif  // BSHT_GAME_LOCAL_BOARD_H
