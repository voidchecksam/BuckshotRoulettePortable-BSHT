#include "game/local_board.h"

#include "game/save_store.h"

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace game {

namespace {

int64_t IntOf(const SaveMap& m, const std::string& key) {
    const auto it = m.find(key);
    if (it == m.end()) return 0;
    if (const auto* i = std::get_if<int64_t>(&it->second)) return *i;
    return 0;
}

std::string StringOf(const SaveMap& m, const std::string& key) {
    const auto it = m.find(key);
    if (it == m.end()) return std::string();
    if (const auto* s = std::get_if<std::string>(&it->second)) return *s;
    return std::string();
}

void Rank(std::vector<BoardRun>& runs) {
    std::stable_sort(runs.begin(), runs.end(),
                     [](const BoardRun& a, const BoardRun& b) { return RankScore(a) > RankScore(b); });
    if (runs.size() > LocalBoard::kMaxRuns) runs.resize(LocalBoard::kMaxRuns);
}

}  // namespace

std::string BoardName(std::string_view waiverName) {
    const std::size_t first = waiverName.find_first_not_of(' ');
    if (first == std::string_view::npos) return std::string();
    const std::size_t last = waiverName.find_last_not_of(' ');
    return std::string(waiverName.substr(first, last - first + 1));
}

int64_t CompressedScore(int64_t val) {
    if (val <= 0) return 0;   // log() of it has no exponent to keep
    const double v = static_cast<double>(val);
    const double scale = std::pow(2.0, 17);   // 2 ** accuracy_bits
    double a = std::max(0.0, std::floor(std::log(v) / std::log(2.0)) + 1 - 17);
    if (scale * (std::pow(2.0, a) - 1) > v) a -= 1;
    const double b = (v - scale * (std::pow(2.0, a) - 1)) / std::pow(2.0, a);
    return static_cast<int64_t>(std::floor(a * scale + b));
}

int64_t RankScore(const BoardRun& run) {
    return CompressedScore(ShownScore(run));
}

int64_t ShownScore(const BoardRun& run) {
    int64_t sc = run.initial;
    for (int64_t s = 0; s < run.beaten - 1; ++s) {
        if (sc > INT64_MAX / 2) return INT64_MAX;   // GDScript wraps; a board entry should not
        sc *= 2;
    }
    return sc;
}

void LocalBoard::Load(const SaveStore* store) {
    runs_.clear();
    latest_.clear();
    SaveMap m;
    if (!store || !store->Load(kEntry, m)) return;
    latest_ = BoardName(StringOf(m, "latest"));
    const int64_t count = std::min<int64_t>(IntOf(m, "count"), static_cast<int64_t>(kMaxRuns));
    for (int64_t i = 0; i < count; ++i) {
        const std::string n = std::to_string(i);
        runs_.push_back(BoardRun{BoardName(StringOf(m, "name" + n)), IntOf(m, "initial" + n), IntOf(m, "beaten" + n)});
    }
    Rank(runs_);
}

bool LocalBoard::Append(const SaveStore* store, const BoardRun& run) {
    if (!store) return false;
    Load(store);
    runs_.push_back(BoardRun{BoardName(run.name), run.initial, run.beaten});
    Rank(runs_);
    latest_ = BoardName(run.name);
    SaveMap m;
    m["count"] = static_cast<int64_t>(runs_.size());
    m["latest"] = latest_;
    for (std::size_t i = 0; i < runs_.size(); ++i) {
        const std::string n = std::to_string(i);
        m["name" + n] = runs_[i].name;
        m["initial" + n] = runs_[i].initial;
        m["beaten" + n] = runs_[i].beaten;
    }
    return store->Save(kEntry, m);
}

}  // namespace game
