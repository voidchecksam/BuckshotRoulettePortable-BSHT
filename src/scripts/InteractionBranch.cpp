#include "scripts/InteractionBranch.h"

#include "scripts/SignatureButtonBranch.h"

namespace scripts {

namespace {
// get_parent().get_child(3). Godot returns null (after an error) when out of range.
godot::Node* FourthSibling(godot::Node* self) {
    godot::Node* parent = self ? self->get_parent() : nullptr;
    return parent && parent->get_child_count() > 3 ? parent->get_child(3) : nullptr;
}
}  // namespace

// gd: L16-L20
void InteractionBranch::_ready() {
    // gd: the typed export takes the node's SignButton script.
    if (signatureButton_letterAlias != "") assignedSignatureButton = godot::script_cast<SignButton>(FourthSibling(get_node_owner()));
    if (signatureButton_specialAlias != "") assignedSignatureButton = godot::script_cast<SignButton>(FourthSibling(get_node_owner()));
}

}  // namespace scripts
