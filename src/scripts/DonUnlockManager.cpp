#include "scripts/DonUnlockManager.h"

#include "game/save_store.h"
#include "scripts/AchievementManager.h"
#include "scripts/DialogueManager_Ending.h"
#include "godot/print.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"

#include <cstdint>
#include <variant>

namespace scripts {

namespace {
constexpr const char* kAmount = "total_amount_selected";
}

// gd: L23-L29
void Unlocker::UnlockMode() {
    if (!store) return;
    game::SaveMap data{{kAmount, std::int64_t{0}}};
    store->Save(savepath, data);
}

// gd: L31-L45
void Unlocker::IncrementAmount() {
    if (!Unlocked()) return;
    game::SaveMap data;
    if (!store->Load(savepath, data)) return;
    std::int64_t amount = 0;
    if (const auto it = data.find(kAmount); it != data.end()) {
        if (const auto* v = std::get_if<std::int64_t>(&it->second)) amount = *v;
    }
    amount += 1;
    game::SaveMap new_data{{kAmount, amount}};
    store->Save(savepath, new_data);
    if (amount >= 10 && ach) ach->UnlockAchievement("ach4");
}

bool Unlocker::Unlocked() const {
    return store && store->Exists(savepath);
}

}  // namespace scripts

namespace scripts {

// gd: L10-L21
seq::Task Unlocker::UnlockRoutine() {
    UnlockMode();
    co_await get_tree()->create_timer(1);
    dia->overriding = true;
    dia->dialogueUI = ui;
    dia->HideText();
    dia->ShowText_Forever(godot::tr(t));
    co_await get_tree()->create_timer(4);
    anim->play("fade");
    co_await get_tree()->create_timer(1);
    godot::print("changing scene to: menu");
    get_tree()->change_scene_to_file("res://scenes/menu.tscn");
}

}  // namespace scripts
