#include "scripts/MedicineManager.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "scripts/CameraManager.h"
#include "scripts/DealerIntelligence.h"
#include "scripts/CameraShaker.h"
#include "scripts/DeathManager.h"
#include "scripts/HealthCounter.h"
#include "scripts/ItemInteraction.h"
#include "scripts/RoundManager.h"

namespace scripts {

bool Medicine::call(const std::string& method, const std::vector<godot::Variant>& /*args*/) {
    if (method == "UseMedicine_Dealer") {
        get_tree()->spawn(UseMedicine_Dealer());
        return true;
    }
    return false;
}

// gd: L17-L38
seq::Task Medicine::UseMedicine() {
    cam->moving = false;
    const bool dying = GetFlip();
    // gd: Perms(...), not awaited: spawned.
    if (dying) {
        get_tree()->spawn(Perms(8.78));
    } else {
        get_tree()->spawn(Perms(6.78));
    }
    co_await get_tree()->create_timer(3.05);
    death->DisableSpeakers();
    co_await get_tree()->create_timer(1.25 + .1);
    if (dying) {
        counter->skipping_careful = true;
        rm->health_player -= 1;
        speaker_medicine->set_stream(death_player);
        speaker_medicine->play();
        co_await death->MedicineDeath();
        counter->overriding_medicine = true;
        counter->overriding_medicine_adding = false;
        counter->UpdateDisplayRoutineCigarette_Player();
    } else {
        death->FadeInSpeakers();
        counter->overriding_medicine = true;
        counter->overriding_medicine_adding = true;
        counter->UpdateDisplayRoutineCigarette_Player();
    }
}

// gd: L40-L66
seq::Task Medicine::UseMedicine_Dealer() {
    const bool dying = dealerDying;
    if (dying) {
        co_await get_tree()->create_timer(5);
        speaker_medicine->set_stream(death_dealer);
        speaker_medicine->play();
        anim_dealerhands->play("dealer death medicine");
        co_await get_tree()->create_timer(.41);
        death->cameraShaker->Shake();
        co_await get_tree()->create_timer(.6);
        // gd: #rm.health_opponent -= 1, commented out in the .gd.
        counter->overriding_medicine = true;
        counter->overriding_medicine_adding = false;
        counter->UpdateDisplayRoutineCigarette_Enemy();
        co_await get_tree()->create_timer(.5);
        anim_dealerhands->play("RESET");
        co_await get_tree()->create_timer(2);
        dealerDying = false;
        get_tree()->spawn(dealerai->DealerChoice());   // called without await in the .gd
    } else {
        co_await get_tree()->create_timer(4.07);
        counter->overriding_medicine = true;
        counter->overriding_medicine_adding = true;
        counter->UpdateDisplayRoutineCigarette_Enemy();
        co_await get_tree()->create_timer(2);
        dealerDying = false;
        get_tree()->spawn(dealerai->DealerChoice());   // called without await in the .gd
    }
}

// gd: L68-L70
seq::Task Medicine::Perms(double d) {
    co_await get_tree()->create_timer(d);
    interaction->EnablePermissions();
}

// gd: L72-L75. One randf_range draw; below .5 is the heal.
bool Medicine::GetFlip() {
    const double value = godot::randf_range(0.0, 1.0);
    if (value < .5) return false;
    return true;
}

}  // namespace scripts
