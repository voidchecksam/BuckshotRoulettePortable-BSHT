# bsht_core links nothing.
#
# core/ and seq/ stay free of SDL, GL and every platform header, so the
# simulation remains testable headless and portable to arm-vita-eabi. The
# core_has_no_platform_deps test enforces it.
add_library(bsht_core STATIC
    core/core.cpp
    core/round_table.cpp
    core/stream_resampler.cpp
    seq/scheduler.cpp
)
target_include_directories(bsht_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# assets/ parses the extracted data. It is a LAYER ABOVE core/, not part of it,
# but it is equally free of SDL and GL so it stays unit-testable headless and
# compiles for arm-vita-eabi unchanged. The no-platform-deps test covers it too.
# The vendored Vorbis decoder. Its own target so the dependency is visible in
# the build graph rather than buried in a source list, and so the guard test can
# assert that only the audio layer touches it.
add_library(bsht_vorbis STATIC
    ${CMAKE_SOURCE_DIR}/third_party/stb_vorbis_impl.c
)
target_include_directories(bsht_vorbis PUBLIC ${CMAKE_SOURCE_DIR}/third_party)
# Third-party code, not ours to clean up: do not fail our build on its warnings.
if(NOT MSVC)
    target_compile_options(bsht_vorbis PRIVATE -w)
endif()

add_library(bsht_assets STATIC
    assets/glb.cpp
    assets/scene.cpp
    assets/bake.cpp
    assets/mesh_index.cpp
    assets/room.cpp
    assets/audio_streams.cpp
    assets/room_file.cpp
    assets/animation.cpp
    assets/animated_nodes.cpp
    assets/node_table.cpp
    assets/sound_bank.cpp
    assets/wav.cpp
    assets/ui_layout.cpp
    assets/font.cpp
    assets/label3d.cpp
    assets/texture_pool.cpp
    assets/inline_meshes.cpp
    assets/dxt1.cpp
    assets/light_model.cpp
    assets/scene_fog.cpp
    assets/light_bvh.cpp
    assets/surface_bake.cpp
    assets/probe_grid.cpp
    assets/raw_image.cpp
)
target_include_directories(bsht_assets PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bsht_assets PUBLIC bsht_core)

# godot/ is the Godot-shaped runtime the translated scripts run on. Headless
# like core/ and assets/, so every engine rule it implements is unit- and
# parity-testable without a window, and it compiles for arm-vita-eabi
# unchanged. Guarded by core_has_no_platform_deps.
add_library(bsht_godot STATIC
    godot/node.cpp
    godot/audio_spatial.cpp
    godot/material.cpp
    godot/scene_tree.cpp
    godot/particles.cpp
    godot/particle_sim.cpp
    godot/skeleton.cpp
    godot/runtime_scene_loader.cpp
    godot/animation.cpp
    godot/animation_player.cpp
    godot/script.cpp
    godot/translation.cpp
    godot/print.cpp
    godot/packed_scene.cpp
    godot/input_event.cpp
    godot/input_map.cpp
    godot/input.cpp
    godot/gui.cpp
    godot/physics.cpp
    godot/camera_projection.cpp
)
target_include_directories(bsht_godot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bsht_godot PUBLIC bsht_assets bsht_core)

# scripts/ is the GDScript translated 1:1: one class per scripts/*.gd, same
# names, bound to the scene by the generated bindings.cpp
# (tools/gen_script_bindings.py). Headless, like godot/.
add_library(bsht_scripts STATIC
    scripts/bind_support.cpp
    scripts/bindings.cpp
    scripts/BlinkerBranch.cpp
    scripts/ButtonClass.cpp
    scripts/ButtonClass_Main.cpp
    scripts/CameraManager.cpp
    scripts/CompartmentManager.cpp
    scripts/ControllerManager.cpp
    scripts/CursorManager.cpp
    scripts/DealerIntelligence.cpp
    scripts/Debugging.cpp
    scripts/DecisionTextManager.cpp
    scripts/DescriptionManager.cpp
    scripts/DialogueManager.cpp
    scripts/HandcuffManager.cpp
    scripts/HealthCounter.cpp
    scripts/InteractionBranch.cpp
    scripts/InteractionManager.cpp
    scripts/IntroManager.cpp
    scripts/ItemManager.cpp
    scripts/ItemInteraction.cpp
    scripts/ShellExamine.cpp
    scripts/BurnerPhone.cpp
    scripts/SegmentManager.cpp
    scripts/DefibBlade.cpp
    scripts/DefibCutter.cpp
    scripts/MedicineManager.cpp
    scripts/HandManager.cpp
    scripts/MouseRaycast.cpp
    scripts/MusicManager.cpp
    scripts/CameraShaker.cpp
    scripts/ObjectShaker.cpp
    scripts/DeathManager.cpp
    scripts/FilterController.cpp
    scripts/ShellDropManager.cpp
    scripts/SignatureManager.cpp
    scripts/BlockoutManager.cpp
    scripts/BpmLight.cpp
    scripts/DonUnlockManager.cpp
    scripts/UserExit.cpp
    scripts/GlobalVariables.cpp
    scripts/ChecmarkBranch.cpp
    scripts/ColorblindManager.cpp
    scripts/StatueManager.cpp
    scripts/SceneChangeNotification.cpp
    scripts/PipelineManager.cpp
    scripts/DialogueManager_Ending.cpp
    scripts/TextFire.cpp
    scripts/EndingManager.cpp
    scripts/BriefcaseMachine.cpp
    scripts/HeavenManager.cpp
    scripts/EndlessMode.cpp
    scripts/LeaderboardManager.cpp
    scripts/CrtManager.cpp
    scripts/CrtButtonBranch.cpp
    scripts/CrtIcon.cpp
    scripts/PartitionBranch.cpp
    scripts/DotRemover.cpp
    scripts/ItemGridIndicator.cpp
    scripts/GameOverManager.cpp
    scripts/JankPreventer.cpp
    scripts/ResetManager.cpp
    scripts/SignatureButtonBranch.cpp
    scripts/ShellEjectManager.cpp
    scripts/SmokeController.cpp
    scripts/SpeakerController3D.cpp
    scripts/TimeScaleManager.cpp
    scripts/PermissionManager.cpp
    scripts/PickupIndicator.cpp
    scripts/RoundManager.cpp
    scripts/ShellBranch.cpp
    scripts/ShellLoader.cpp
    scripts/ShellSpawner.cpp
    scripts/ShotgunShooting.cpp
    scripts/SpeakerController.cpp
    scripts/TextInteraction.cpp
    scripts/TextShaker.cpp
    scripts/AchievementManager.cpp
    scripts/MenuManager.cpp
    scripts/SaveFileManager.cpp
    scripts/TempDeathScreen.cpp
    scripts/OptionsManager.cpp
    scripts/KeyRebinding.cpp
    # Persistence and achievements: game/ code the translated scripts call
    # (SaveFileManager, OptionsManager, Achievement), so it links here.
    game/save_store.cpp
    game/achievements.cpp
    game/local_board.cpp
    game/stream_router.cpp
)
target_include_directories(bsht_scripts PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bsht_scripts PUBLIC bsht_godot bsht_assets bsht_core)

# game/ is the turn structure joining core/ + seq/ to the presentation layers.
# Also platform-free: a whole round can be played headlessly in a test.
add_library(bsht_game STATIC
    game/session.cpp
    game/music_router.cpp
    game/ui_screen.cpp
    # The loading screen's LAYOUT half. Headless, with the game sources, so the
    # tests can drive it; app/loading_screen_draw.cpp is the half that links GL.
    app/loading_screen.cpp
    # Parity scenarios, frame by frame: bsht_parity samples them, the desktop
    # exe renders them (--scenario). Headless.
    app/scenario_runner.cpp
    app/scenario_boot.cpp
    app/device_features.cpp
    app/ui_screens.cpp
    # Which files make up each scene, for change_scene_to_file.
    app/scene_host.cpp
    # bsht-extract's game folder, checked before anything reads it.
    app/data_check.cpp
    app/data_layout.cpp
    app/desktop_paths.cpp
    app/data_screen.cpp
    ${CMAKE_SOURCE_DIR}/third_party/font8x8/font8x8_basic.c
    # The ACHIEVEMENTS screen and toast, layout half.
    app/achievements_screen.cpp
    app/port_branding.cpp
    app/light_uniforms.cpp
    # Diagnostic switches the device can set (env var, or a file in the save dir).
    app/debug_switch.cpp
    # One crash report per boot on the Vita, numbered (never truncate the last).
    app/report_files.cpp
    # Prop batching's headless half (chunking, index lists); the GL half is
    # render/scene_renderer.cpp.
    render/prop_batch_plan.cpp
    # GPU skinning's headless half (bone stream, clip palette, sources).
    render/skin_gpu_plan.cpp
    debug/state.cpp
    debug/report.cpp
    debug/overlay.cpp
    debug/log.cpp
    debug/frame_timing.cpp
    debug/hw_probe.cpp
)
target_include_directories(bsht_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(bsht_game PUBLIC bsht_scripts bsht_godot bsht_assets bsht_core)

# The offline baker. Host-only: it reads the glTF library, which never goes
# near the device. Links only the headless layers, so it builds on a machine
# with no graphics stack at all.
if(NOT BSHT_TARGET STREQUAL "vita")
    # bake_textures.cpp is the only translation unit that links an image
    # decoder, and it is a host tool. The device ships no codec.
    # The surface bake's page builder: xatlas and stb_dxt live in this
    # host-only library and nowhere the device links.
    add_library(bsht_surface_bake STATIC
        ${CMAKE_SOURCE_DIR}/tools/bake_surfaces.cpp
        ${CMAKE_SOURCE_DIR}/third_party/xatlas/xatlas.cpp)
    target_include_directories(bsht_surface_bake PUBLIC
        ${CMAKE_SOURCE_DIR}/third_party
        ${CMAKE_SOURCE_DIR}/tools)
    target_link_libraries(bsht_surface_bake PUBLIC bsht_assets bsht_core)
    if(NOT MSVC)
        set_source_files_properties(${CMAKE_SOURCE_DIR}/third_party/xatlas/xatlas.cpp
            PROPERTIES COMPILE_OPTIONS "-w")
    endif()

    # bsht-extract's libraries. reproc's own CMake builds its C library; its
    # C++ wrapper, tests and install rules stay off.
    set(REPROC++ OFF CACHE BOOL "" FORCE)
    set(REPROC_TEST OFF CACHE BOOL "" FORCE)
    set(REPROC_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(REPROC_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/reproc ${CMAKE_BINARY_DIR}/reproc EXCLUDE_FROM_ALL)

    add_library(bsht_ogg STATIC
        ${CMAKE_SOURCE_DIR}/third_party/libogg/src/framing.c
        ${CMAKE_SOURCE_DIR}/third_party/libogg/src/bitwise.c)
    target_include_directories(bsht_ogg PUBLIC ${CMAKE_SOURCE_DIR}/third_party/libogg/include)
    if(NOT MSVC)
        target_compile_options(bsht_ogg PRIVATE -w)
    endif()

    add_library(bsht_extract STATIC
        extract/pack_reader.cpp
        extract/game_version.cpp
        extract/find_game.cpp
        extract/fingerprint.cpp
        extract/unpack.cpp
        extract/godot_run.cpp
        extract/self_path.cpp
        extract/ogg_rebuild.cpp
        extract/exporters.cpp
        extract/package.cpp
        extract/pipeline.cpp
        extract/remove_tree.cpp
        extract/preflight.cpp
        extract/arguments.cpp)
    target_include_directories(bsht_extract PUBLIC ${CMAKE_SOURCE_DIR}/src
                               ${CMAKE_SOURCE_DIR}/third_party/picosha2/include)
    target_link_libraries(bsht_extract PUBLIC bsht_core reproc bsht_ogg bsht_vorbis)

    # The bakes as functions (tools/bake_main.h): the dev programs below and
    # bsht-extract call the same code.
    add_library(bsht_bake STATIC
        ${CMAKE_SOURCE_DIR}/tools/bake_room.cpp
        ${CMAKE_SOURCE_DIR}/tools/bake_textures.cpp
        ${CMAKE_SOURCE_DIR}/tools/bake_audio.cpp)
    # bsht_godot: the baker reads runtime_main.json to keep geometry Godot does
    # not draw at load out of the merge (render/prop_visibility.h is header-only).
    target_link_libraries(bsht_bake PUBLIC bsht_surface_bake bsht_scripts bsht_godot bsht_assets bsht_core
                          bsht_vorbis)
    target_include_directories(bsht_bake PUBLIC
        ${CMAKE_SOURCE_DIR}/third_party
        ${CMAKE_SOURCE_DIR}/tools)

    add_executable(bake_room ${CMAKE_SOURCE_DIR}/tools/bake_room_cli.cpp)
    target_link_libraries(bake_room PRIVATE bsht_bake)

    # Debug: dump the page texels a prop samples (tools/dump_prop_page.cpp).
    add_executable(dump_prop_page ${CMAKE_SOURCE_DIR}/tools/dump_prop_page.cpp)
    target_link_libraries(dump_prop_page PRIVATE bsht_assets bsht_core)

    add_executable(bake_audio ${CMAKE_SOURCE_DIR}/tools/bake_audio_cli.cpp)
    target_link_libraries(bake_audio PRIVATE bsht_bake)

    # The release tool: players run it on their own copy of the game.
    add_executable(bsht-extract ${CMAKE_SOURCE_DIR}/tools/extract/main.cpp)
    target_link_libraries(bsht-extract PRIVATE bsht_extract bsht_bake)
    if(WIN32)
        target_sources(bsht-extract PRIVATE platform/windows/utf8.rc)
        target_link_libraries(bsht-extract PRIVATE shell32)
    endif()
    target_compile_definitions(bsht-extract PRIVATE BSHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

    # Dev: a game folder from assets/ (bsht_desktop --data, Vita3K).
    add_executable(stage_game ${CMAKE_SOURCE_DIR}/tools/stage_game.cpp)
    target_link_libraries(stage_game PRIVATE bsht_extract)

    # The port's playback of every bank cue, for tools/compare_cues.py.
    add_executable(render_cues ${CMAKE_SOURCE_DIR}/tools/render_cues.cpp)
    target_link_libraries(render_cues PRIVATE bsht_assets bsht_core)
endif()

# ---------------------------------------------------------------------------
# The graphics layers. Shared between targets: render/ is written against the
# GLES2-equivalent subset, and gl_compat.h carries the only permitted platform
# branch, so the same sources build for both.
# ---------------------------------------------------------------------------
set(BSHT_RENDER_SOURCES
    # The game as both entry points run it. With the graphics layers because
    # it draws; the platform itself stays in main_desktop.cpp / main_vita.cpp.
    app/game_app.cpp
    app/audio_wiring.cpp
    platform/audio.cpp
    render/shader.cpp
    render/mesh_gpu.cpp
    render/prop_chunk_gpu.cpp
    render/scene_renderer.cpp
    render/post.cpp
    render/screenshot.cpp
    render/sprite_batch.cpp
    render/label3d_renderer.cpp
    render/particle_renderer.cpp
    # The overlay's DRAWING half only. debug/overlay.cpp is headless
    # and lives with the game sources so the tests can drive it.
    debug/overlay_draw.cpp
    # The loading screen's DRAWING half only. app/loading_screen.cpp is headless
    # and lives with the game sources so the tests can drive it.
    app/loading_screen_draw.cpp
    app/data_screen_draw.cpp
)

if(BSHT_TARGET STREQUAL "vita")
    include("${CMAKE_SOURCE_DIR}/cmake/vita.cmake")

    # window.cpp is desktop-only: vitaGL owns the context and there is no
    # window to manage. Input comes from sceCtrl in main_vita.cpp, and the
    # front touch panel from platform/vita_touch.cpp, the one place
    # psp2/touch.h is included, so the entry point stays platform wiring and
    # the edge detection (platform/touch_tracker.h) stays testable on desktop.
    add_library(bsht_platform STATIC ${BSHT_RENDER_SOURCES} platform/vita_touch.cpp platform/vita_shader_cache.cpp)
    target_include_directories(bsht_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(bsht_platform PUBLIC bsht_game bsht_godot bsht_assets bsht_core bsht_vorbis)
    target_compile_definitions(bsht_platform PUBLIC BSHT_SAVE_DIR="ux0:data/bsht"
                                                    BSHT_ROOM_ROOT="app0:data")

    add_executable(bsht_vita main_vita.cpp platform/alloc_count.cpp)
    # vita-toolchain #150: the linker places the data segment without room for
    # the SCE module info vita-elf-create appends to segment 0. When .text ends
    # within ~3 KB of a 64 KiB boundary the old tool corrupts its heap
    # (0xC0000374) and newer ones refuse the ELF. The vitasdk linker script
    # honours __sce_headroom; 64 KiB costs address space, not vpk size.
    target_link_options(bsht_vita PRIVATE -Wl,--defsym=__sce_headroom=0x10000)
    target_link_libraries(bsht_vita PRIVATE
        bsht_platform bsht_assets bsht_game bsht_core bsht_vorbis
        SDL2
        vitaGL vitashark SceShaccCg_stub SceShaccCgExt mathneon
        SceKernelDmacMgr_stub
        SceGxm_stub SceDisplay_stub SceCtrl_stub
        SceAppMgr_stub SceAppUtil_stub
        SceAudio_stub SceAudioIn_stub
        SceSysmodule_stub SceIofilemgr_stub
        SceCommonDialog_stub SceTouch_stub SceHid_stub
        SceMotion_stub ScePower_stub SceProcessmgr_stub
        SceIme_stub
        taihen_stub
        m)

    # Pinned: one title id, so each install replaces the one bubble. A build
    # installed over a different id leaves the launcher starting a stale
    # bubble.
    set(BSHT_VITA_TITLE_ID "BSHT00016")

    # Stamped into line 1 of the report, so a returned report whose stamp does
    # not match the vpk that was sent is seen to be stale.
    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE BSHT_GIT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    if(NOT BSHT_GIT_SHA)
        set(BSHT_GIT_SHA "nogit")
    endif()
    string(TIMESTAMP BSHT_BUILD_STAMP "%Y-%m-%dT%H:%M:%SZ" UTC)

    target_compile_definitions(bsht_vita PRIVATE
        BSHT_TITLE_ID="${BSHT_VITA_TITLE_ID}"
        BSHT_BUILD_STAMP="${BSHT_BUILD_STAMP}"
        BSHT_GIT_SHA="${BSHT_GIT_SHA}")

    vita_package(bsht_vita "${BSHT_VITA_TITLE_ID}" "Buckshot Roulette")

    return()
endif()

# ---------------------------------------------------------------------------
# Desktop. SDL2/OpenGL are optional so a machine with no graphics stack can
# still build bsht_core and run the whole headless test suite: the simulation
# is never gated on a graphics stack being present.
# ---------------------------------------------------------------------------
find_package(SDL2 QUIET)
find_package(OpenGL QUIET)

if(NOT SDL2_FOUND OR NOT OPENGL_FOUND)
    message(STATUS "SDL2/OpenGL not found -- building headless targets only")
    return()
endif()

add_library(bsht_platform STATIC
    platform/window.cpp
    ${BSHT_RENDER_SOURCES}
)
target_include_directories(bsht_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(bsht_platform PUBLIC BSHT_SAVE_DIR="save" BSHT_ROOM_ROOT="assets/baked")

# SDL2::SDL2 is not defined by every SDL2 packaging (MSYS2's sdl2-config module
# exports plain variables instead), so fall back rather than hard-fail.
if(TARGET SDL2::SDL2)
    target_link_libraries(bsht_platform PUBLIC bsht_game bsht_godot bsht_assets bsht_core bsht_vorbis SDL2::SDL2 OpenGL::GL)
else()
    target_include_directories(bsht_platform PUBLIC ${SDL2_INCLUDE_DIRS})
    target_link_libraries(bsht_platform PUBLIC bsht_game bsht_godot bsht_assets bsht_core bsht_vorbis ${SDL2_LIBRARIES} OpenGL::GL)
endif()

add_executable(bsht_desktop main_desktop.cpp platform/alloc_count.cpp)
target_link_libraries(bsht_desktop PRIVATE bsht_platform bsht_assets bsht_game bsht_core)
if(WIN32)
    target_sources(bsht_desktop PRIVATE platform/windows/utf8.rc)
endif()

