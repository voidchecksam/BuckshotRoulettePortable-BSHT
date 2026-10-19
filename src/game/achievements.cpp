#include "game/achievements.h"

#include "core/json_lite.h"
#include "game/save_store.h"

#include <cstdio>

namespace game {
namespace {

constexpr const char* kSaveName = "achievements";

bool ReadWholeFile(const char* path, std::string& out) {
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char chunk[4096];
    size_t got = 0;
    out.clear();
    while ((got = std::fread(chunk, 1, sizeof chunk, f)) > 0) out.append(chunk, got);
    std::fclose(f);
    return true;
}

}  // namespace

bool Achievements::LoadText(const char* jsonPath) {
    text_.clear();
    std::string json;
    if (!ReadWholeFile(jsonPath, json)) return false;
    const char* b = nullptr;
    const char* e = nullptr;
    if (!core::json::FindArrayTopLevel(json.data(), json.data() + json.size(), "achievements", &b, &e)) {
        return false;
    }
    std::vector<core::json::Span> objects;
    core::json::SplitObjects(b, e, objects);
    for (const auto& [ob, oe] : objects) {
        AchievementText t;
        t.id = core::json::ReadStringTopLevelDecoded(ob, oe, "id");
        t.name = core::json::ReadStringTopLevelDecoded(ob, oe, "name");
        t.description = core::json::ReadStringTopLevelDecoded(ob, oe, "description");
        t.hidden = core::json::ReadBoolTopLevel(ob, oe, "hidden", false);
        if (!t.id.empty()) text_.push_back(std::move(t));
    }
    return !text_.empty();
}

void Achievements::Attach(const SaveStore* store) {
    store_ = store;
    unlocked_.clear();
    if (!store_) return;
    SaveMap saved;
    if (!store_->Load(kSaveName, saved)) return;
    for (const auto& [id, value] : saved) {
        if (const bool* on = std::get_if<bool>(&value); on && *on) unlocked_.insert(id);
    }
}

bool Achievements::Unlock(const std::string& id) {
    const AchievementText* text = Find(id);
    if (!text) {
        Say("ach: unknown achievement " + id);
        return false;
    }
    if (!unlocked_.insert(id).second) return false;
    Say("ach: unlocked " + id + " (" + text->name + ")");
    if (store_) {
        SaveMap values;
        for (const std::string& u : unlocked_) values[u] = true;
        if (!store_->Save(kSaveName, values)) Say("ach: the achievements save could not be written");
    }
    toasts_.push_back(*text);
    return true;
}

bool Achievements::IsUnlocked(const std::string& id) const {
    return unlocked_.count(id) != 0;
}

bool Achievements::PopToast(AchievementText& out) {
    if (toasts_.empty()) return false;
    out = std::move(toasts_.front());
    toasts_.pop_front();
    return true;
}

void Achievements::Say(const std::string& line) const {
    if (log_) log_(line);
}

const AchievementText* Achievements::Find(const std::string& id) const {
    for (const AchievementText& t : text_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

}  // namespace game
