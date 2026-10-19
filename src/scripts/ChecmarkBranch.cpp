#include "scripts/ChecmarkBranch.h"

namespace scripts {

// gd: L7-L9
void Checkmark::UpdateCheckmark(bool state) {
    if (state) ui->set_texture(checkmark_active);
    else ui->set_texture(checkmark_ianctive);
}

}  // namespace scripts
