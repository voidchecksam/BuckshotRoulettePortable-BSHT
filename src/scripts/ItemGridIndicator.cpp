#include "scripts/ItemGridIndicator.h"

#include "godot/scene_tree.h"
#include "scripts/RoundManager.h"
#include "scripts/resources.h"

namespace scripts {

// gd: L7-L12
void GridIndicator::ShowGridIndicator() {
    parent->set_visible(true);
    animator->play("show");
    roundManager->playerData->indicatorShown = true;
    get_tree()->spawn(HideRoutine(), get_node_owner());
}

seq::Task GridIndicator::HideRoutine() {
    co_await get_tree()->create_timer(1);
    parent->set_visible(false);
}

}  // namespace scripts
