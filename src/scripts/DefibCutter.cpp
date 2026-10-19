#include "scripts/DefibCutter.h"

#include "godot/rng.h"
#include "godot/scene_tree.h"
#include "godot/translation.h"
#include "scripts/CameraManager.h"
#include "scripts/DefibBlade.h"
#include "scripts/DialogueManager.h"
#include "scripts/RoundManager.h"

namespace scripts {

// gd: L24-L50
seq::Task DefibCutter::InitialSetup() {
    cam->BeginLerp("defib setup");
    co_await get_tree()->create_timer(.6);
    animator_main->play("initial setup");
    speaker_motor->play();
    co_await get_tree()->create_timer(3.6);
    cam->BeginLerp("defib console");
    speaker_bootup->play();
    co_await get_tree()->create_timer(1.9);
    for (godot::Node3D* console : consoleArray) {
        console->set_visible(true);
        speaker_beep->set_pitch_scale(static_cast<float>(godot::randf_range(.8, 1.0)));
        speaker_beep->play();
        co_await get_tree()->create_timer(.09);
    }
    co_await get_tree()->create_timer(.4);
    cam->BeginLerp("defib setup");
    co_await get_tree()->create_timer(.6);
    blade_player->Blade("open", true);
    blade_dealer->Blade("open", false);
    co_await get_tree()->create_timer(.5);
    animator_main->play("main setup");
    speaker_blademove->play();
    co_await get_tree()->create_timer(.6);
    cam->BeginLerp("defib blade insert");
    co_await get_tree()->create_timer(1.8);
    cam->BeginLerp("home");
    co_await get_tree()->create_timer(.6);
}

// gd: L52-L77
seq::Task DefibCutter::CutWire(std::string who) {
    const std::string tempwho = who;
    const std::string origsocket = cam->activeSocket;
    cam->BeginLerp("enemy");
    co_await get_tree()->create_timer(3);
    if (!cutDialogueSent) {
        dia->ShowText_Forever(godot::tr("ARE YOU READY"));
        co_await get_tree()->create_timer(4);
        dia->HideText();
        co_await get_tree()->create_timer(.2);
        cutDialogueSent = true;
    }
    cam->BeginLerp("defib blade insert");
    co_await get_tree()->create_timer(1);
    if (who == "player") blade_player->Blade("close", true);
    if (who == "dealer") blade_dealer->Blade("close", true);
    roundManager->requestedWireCut = false;
    roundManager->wireToCut = "";
    co_await get_tree()->create_timer(.05);
    BreakDisplay(tempwho);
    co_await get_tree()->create_timer(.8);
    cam->BeginLerp("health counter");
    co_await get_tree()->create_timer(.6);
    // gd: FlashError(tempwho), not awaited: spawned.
    get_tree()->spawn(FlashError(tempwho));
    co_await get_tree()->create_timer(.8);
    cam->BeginLerp(origsocket);
    co_await get_tree()->create_timer(1);
}

// gd: L80-L98
seq::Task DefibCutter::FlashError(std::string who) {
    godot::Node3D* act = who == "player" ? errorUI_player : errorUI_dealer;
    act->set_visible(true);
    PlayError();
    co_await get_tree()->create_timer(.05);
    act->set_visible(false);
    co_await get_tree()->create_timer(.05);
    act->set_visible(true);
    PlayError();
    co_await get_tree()->create_timer(.11);
    act->set_visible(false);
    co_await get_tree()->create_timer(.05);
    act->set_visible(true);
    PlayError();
    co_await get_tree()->create_timer(.05);
    act->set_visible(false);
    PlayError();
}

// gd: L100-L113
seq::Task DefibCutter::BlipError(std::string who) {
    godot::Node3D* act = who == "player" ? errorUI_player : errorUI_dealer;
    act->set_visible(true);
    PlayError();
    co_await get_tree()->create_timer(.05);
    act->set_visible(false);
    co_await get_tree()->create_timer(.05);
    act->set_visible(true);
    PlayError();
    co_await get_tree()->create_timer(.11);
    act->set_visible(false);
    PlayError();
}

// gd: L115-L129
seq::Task DefibCutter::BlipError_Both() {
    errorUI_dealer->set_visible(true);
    errorUI_player->set_visible(true);
    PlayError();
    co_await get_tree()->create_timer(.05);
    errorUI_dealer->set_visible(false);
    errorUI_player->set_visible(false);
    co_await get_tree()->create_timer(.05);
    errorUI_dealer->set_visible(true);
    errorUI_player->set_visible(true);
    PlayError();
    co_await get_tree()->create_timer(.11);
    errorUI_dealer->set_visible(false);
    errorUI_player->set_visible(false);
    PlayError();
}

// gd: L133-L141
void DefibCutter::BreakDisplay(std::string who) {
    if (who == "player") {
        healthUI_player->set_visible(false);
        displayBroken_player = true;
    } else {
        healthUI_dealer->set_visible(false);
        displayBroken_dealer = true;
    }
    if (displayBroken_dealer && displayBroken_player) healthUI_divider->set_visible(false);
}

// gd: L143-L147
void DefibCutter::PlayError() {
    const int64_t index = godot::randi_range(0, static_cast<int64_t>(soundArray_error.size()) - 1);
    speaker_error->set_stream(soundArray_error[static_cast<std::size_t>(index)]);
    speaker_error->play();
}

}  // namespace scripts
