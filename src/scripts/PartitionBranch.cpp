#include "scripts/PartitionBranch.h"

#include "godot/array_ops.h"
#include "godot/rng.h"
#include "godot/scene_tree.h"

namespace scripts {

namespace {
constexpr const char* kBlank = "  '   '   '   '   '   '   '  ";
}  // namespace

// gd: L10-L13
void PartitionBranch::_ready() {
    p = godot::cast_to<godot::Label3D>(get_node_owner()->get_parent());
    p->set_text(kBlank);
    sp = godot::cast_to<godot::AudioStreamPlayer>(get_node_owner()->get_parent()->get_child(1));
}

// gd: L15-L18
void PartitionBranch::Loop(bool s) {
    if (s) {
        looping = true;
        get_tree()->spawn(LoopPartitions(), get_node_owner());
    } else {
        looping = false;
        // gd: #sp.pitch_scale = .05
        // gd: #sp.play()
    }
}

// gd: L23-L36
seq::Task PartitionBranch::LoopPartitions() {
    while (looping) {
        for (std::size_t i = 0; i < nums.size(); ++i) {
            if (nums[i] == 0) continue;
            nums[i] = static_cast<int>(godot::randi_range(0, 9));
        }

        p->set_text(godot::str(nums[0]) + " ' " + godot::str(nums[1]) + " ' " + godot::str(nums[2]) + " ' " +
                    godot::str(nums[3]) + " ' " + godot::str(nums[4]) + " ' " + godot::str(nums[5]) + " ' " +
                    godot::str(nums[6]) + " ' " + godot::str(nums[7]));
        sp->set_pitch_scale(static_cast<float>(godot::randf_range(.9, 1.1)));
        sp->play();
        co_await get_tree()->create_timer(d);
        for (const int num : nums) {
            if (num != 0) {
                finished = false;
                break;
            }
            finished = true;
        }
        if (finished) Loop(false);
    }
}

// gd: L38-L42
void PartitionBranch::ResetPartition() {
    looping = false;
    p->set_text(kBlank);
    nums = {9, 9, 9, 9, 9, 9, 9, 9};
    finished = false;
}

}  // namespace scripts
