#include "scripts/BurnerPhone.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/DialogueManager.h"
#include "scripts/ShellSpawner.h"

namespace scripts {

bool BurnerPhone::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "SendDialogue") {
        // A method track does not await: the coroutine runs to its first
        // await now and finishes on its own.
        get_tree()->spawn(SendDialogue());
        return true;
    }
    return false;
}

// gd: L6-L35
seq::Task BurnerPhone::SendDialogue() {
    const std::vector<std::string>& sequence = sh->sequenceArray;
    const int len = static_cast<int>(sequence.size());
    std::string firstpart;
    std::string secondpart;
    std::string fulldia;
    if (len != 1) {
        int randindex = static_cast<int>(godot::randi_range(1, len - 1));
        if (randindex == 8 || randindex == 7) randindex -= 1;
        if (sequence[static_cast<std::size_t>(randindex)] == "blank") {
            secondpart = godot::format(godot::tr("BLANKROUND"), {""});
        } else {
            secondpart = godot::format(godot::tr("LIVEROUND"), {""});
        }
        switch (randindex) {
        case 1: firstpart = godot::tr("SEQUENCE2"); break;
        case 2: firstpart = godot::tr("SEQUENCE3"); break;
        case 3: firstpart = godot::tr("SEQUENCE4"); break;
        case 4: firstpart = godot::tr("SEQUENCE5"); break;
        case 5: firstpart = godot::tr("SEQUENCE6"); break;
        case 6: firstpart = godot::tr("SEQUENCE7"); break;
        default: break;
        }
        // gd: tr() of an already translated string returns it unchanged; kept.
        fulldia = godot::tr(firstpart) + "\n" + "... " + godot::tr(secondpart);
    } else {
        fulldia = godot::tr("UNFORTUNATE");
    }
    dia->ShowText_Forever(fulldia);
    co_await get_tree()->create_timer(3);
    dia->HideText();
}

}  // namespace scripts
