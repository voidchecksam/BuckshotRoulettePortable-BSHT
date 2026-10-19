#include "scripts/DotRemover.h"

#include "godot/scene_tree.h"
#include "godot/translation.h"

namespace scripts {

namespace {

void EraseAll(std::string& s, const std::string& what) {
    for (std::size_t at = s.find(what); at != std::string::npos; at = s.find(what, at)) s.erase(at, what.size());
}

}  // namespace

// gd: L6-L10
void DotFix::_ready() {
    p = godot::cast_to<godot::Label3D>(get_node_owner()->get_parent());
    key = p->get_text();
    get_tree()->spawn(ReadyRoutine(), get_node_owner());
}

seq::Task DotFix::ReadyRoutine() {
    co_await get_tree()->create_timer(1);
    Fix();
}

// gd: L12-L17
void DotFix::Fix() {
    if (key == "TOTAL CASH" && godot::get_locale() == "EN") {
        p->set_text("TOTAL CASH ");
        return;
    }
    if (key == "SHOTS FIRED" && godot::get_locale() == "EN") {
        p->set_text("SHOTS FIRED ");
        return;
    }
    const std::string orig = godot::tr(key);
    // gd: orig.replace(".", "").replace(":", "").replace(<fullwidth colon>, "").replace(":", "")
    std::string modif = orig;
    EraseAll(modif, ".");
    EraseAll(modif, ":");
    EraseAll(modif, "\xEF\xBC\x9A");   // U+FF1A FULLWIDTH COLON, UTF-8
    p->set_text(modif);
}

}  // namespace scripts
