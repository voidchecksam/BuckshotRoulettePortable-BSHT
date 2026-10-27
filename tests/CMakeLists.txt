include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(bsht_tests
    test_json_lite.cpp
    test_seq.cpp
    test_seq_godot_timing.cpp
    test_seq_owner.cpp
    test_scripts_intro.cpp
    test_scripts_endings.cpp
    test_scripts_heaven.cpp
    test_scripts_don.cpp
    test_local_board.cpp
    test_audio_streams.cpp
    test_stream_router.cpp
    test_skeleton.cpp
    test_ima_adpcm.cpp
    test_sfx_voice.cpp
    test_audio_spatial.cpp
    test_bus_clamp.cpp
    test_profiler.cpp
    test_gain_ramp.cpp
    test_godot_filter.cpp
    test_prop_batch_plan.cpp
    test_skin_gpu_plan.cpp
    test_debug_switch.cpp
    test_report_files.cpp
    test_shader_cache_check.cpp
    test_dyn_lights.cpp
    test_port_branding.cpp
    test_particle_sim.cpp
    test_raw_image.cpp
    test_slot_requests.cpp
    test_load_stats.cpp
    test_stream_resampler.cpp
    test_scripts_signature.cpp
    test_rounds.cpp
    test_glb.cpp
    test_scene.cpp
    test_bake.cpp
    test_lighting.cpp
    test_light_model.cpp
    test_surface_bake.cpp
    test_bake_surfaces.cpp
    test_probe_grid.cpp
    test_light_uniforms.cpp
    test_hw_probe.cpp
    test_mesh_index.cpp
    test_room.cpp
    test_room_file.cpp
    test_room_props.cpp
    test_room_templates.cpp
    test_frustum.cpp
    test_animation.cpp
    test_sound_bank.cpp
    test_wav.cpp
    test_ui_layout.cpp
    test_font.cpp
    test_prop_visibility.cpp
    test_debug_census.cpp
    test_debug_overlay.cpp
    test_loading_screen.cpp
    test_inline_meshes.cpp
    test_ui_screen.cpp
    test_trs.cpp
    test_label3d.cpp
    test_godot_rng.cpp
    test_godot_node.cpp
    test_godot_scene_tree.cpp
    test_godot_particles.cpp
    test_godot_runtime_scene.cpp
    test_godot_animation.cpp
    test_godot_animation_player.cpp
    test_godot_script.cpp
    test_godot_instancing.cpp
    test_godot_input.cpp
    test_godot_gui.cpp
    test_godot_physics.cpp
    test_godot_material.cpp
    test_scripts_interaction.cpp
    test_scripts_shooting.cpp
    test_scripts_dealer_coinflip.cpp
    test_scripts_items.cpp
    test_scripts_item_use.cpp
    test_scripts_dealer_items.cpp
    test_seq_abandoned.cpp
    test_platform_viewport_map.cpp
    test_parity_dump.cpp
    # The dump comparison the parity scenarios stand on; its own binary is
    # bsht_parity, but the rules are unit-testable here.
    parity/parity_dump.cpp
    test_json_decode.cpp
    test_scripts_bind_support.cpp
    test_scripts_bindings.cpp
    test_scripts_camera.cpp
    test_scripts_dialogue.cpp
    test_scripts_shell_spawner.cpp
    test_scripts_round_manager.cpp
    test_dxt1.cpp
    test_music_router.cpp
    test_save_store.cpp
    test_achievements.cpp
    test_godot_translation.cpp
    test_godot_print.cpp
    test_scripts_global_variables.cpp
    test_scripts_colorblind.cpp
    test_scripts_v22_small.cpp
    test_font_pages.cpp
    test_scene_host.cpp
    test_data_check.cpp
    test_data_layout.cpp
    test_desktop_paths.cpp
    test_data_screen.cpp
    test_scenario_runner.cpp
    test_scenario_boot.cpp
    test_shot_hold.cpp
    test_device_features.cpp
    test_dialogue_keys.cpp
    test_heap_ledger.cpp
    test_scene_soak.cpp
    test_room_release.cpp
    test_music_cutoff.cpp
    test_ui_screens.cpp
    test_scripts_menu.cpp
    test_scripts_options.cpp
    test_achievements_screen.cpp
    test_pack_reader.cpp
    test_game_version.cpp
    test_find_game.cpp
    test_fingerprint.cpp
    test_unpack.cpp
    test_godot_run.cpp
    test_ogg_rebuild.cpp
    test_package.cpp
    test_pipeline.cpp
    test_remove_tree.cpp
    test_preflight.cpp
    test_arguments.cpp
)
target_link_libraries(bsht_tests PRIVATE bsht_core bsht_assets bsht_godot bsht_scripts bsht_game bsht_surface_bake bsht_extract GTest::gtest_main)
target_include_directories(bsht_tests PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests
                           ${CMAKE_SOURCE_DIR}/tools)

# Tests run with the build directory as CWD, so a relative asset path would
# silently SKIP instead of verifying. Hand them an absolute repo root.
target_compile_definitions(bsht_tests PRIVATE
    BSHT_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
    BSHT_BINARY_DIR="${CMAKE_BINARY_DIR}")

# Stands in for Godot in test_godot_run.cpp.
add_executable(fake_godot helpers/fake_godot.cpp)
if(WIN32)
    target_link_libraries(fake_godot PRIVATE shell32)
endif()
add_dependencies(bsht_tests fake_godot)
target_compile_definitions(bsht_tests PRIVATE BSHT_FAKE_GODOT="$<TARGET_FILE:fake_godot>")

include(GoogleTest)
gtest_discover_tests(bsht_tests)

# Guarantees core/ and seq/ never acquire a platform dependency. See the script
# for why this is a test rather than a convention.
add_test(NAME core_has_no_platform_deps
         COMMAND ${CMAKE_COMMAND}
                 -DSRC=${CMAKE_SOURCE_DIR}/src
                 -P ${CMAKE_SOURCE_DIR}/tests/check_no_platform_deps.cmake)

# Guarantees graphics code stays inside the GLES2-equivalent subset. gl_compat.h
# blocks modern GL that is absent on Vita; this blocks LEGACY GL that desktop
# still exports. Without it, fixed-function calls build and run fine here and
# fail only on hardware, a round trip away.
add_test(NAME graphics_within_gles2_subset
         COMMAND ${CMAKE_COMMAND}
                 -DSRC=${CMAKE_SOURCE_DIR}/src
                 -P ${CMAKE_SOURCE_DIR}/tests/check_gl_subset.cmake)

# bsht-extract points the exporters at its work folder; none may hard-code
# the repo's assets/extracted.
add_test(NAME exporters_write_through_scene_args
    COMMAND ${CMAKE_COMMAND} -DDIR=${CMAKE_SOURCE_DIR}/tools/godot
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_exporter_paths.cmake)

# bsht-extract packages every file a scene loads on the device
# (src/extract/package.cpp). Negative-tested by dropping menu.room.
add_test(NAME vita_package_stages_every_scene_file
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
                 -P ${CMAKE_SOURCE_DIR}/tests/check_vita_package.cmake)
add_test(NAME vita_package_rejects_a_missing_scene_file
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR} -DDROP=menu.room
                 -P ${CMAKE_SOURCE_DIR}/tests/check_vita_package.cmake)

# The vpk is code only: LiveArea art and the port's own files.
add_test(NAME vpk_holds_no_game_data
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
                 -P ${CMAKE_SOURCE_DIR}/tests/check_vpk_no_game_data.cmake)
add_test(NAME vpk_holds_no_game_data_rejects_a_room
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
                 -DVITA_CMAKE=${CMAKE_SOURCE_DIR}/tests/fixtures/vita_with_game_data.cmake
                 -P ${CMAKE_SOURCE_DIR}/tests/check_vpk_no_game_data.cmake)
add_test(NAME vpk_holds_no_game_data_rejects_an_unquoted_source
         COMMAND ${CMAKE_COMMAND} -DROOT=${CMAKE_SOURCE_DIR}
                 -DVITA_CMAKE=${CMAKE_SOURCE_DIR}/tests/fixtures/vita_with_unquoted_game_data.cmake
                 -P ${CMAKE_SOURCE_DIR}/tests/check_vpk_no_game_data.cmake)
# A negative test passes only on its own guard's message, not on any failure.
set_tests_properties(vpk_holds_no_game_data_rejects_a_room vpk_holds_no_game_data_rejects_an_unquoted_source
    PROPERTIES PASS_REGULAR_EXPRESSION "would carry game data:[ \r\n]+data/full_world\\.room")
set_tests_properties(vita_package_rejects_a_missing_scene_file
    PROPERTIES PASS_REGULAR_EXPRESSION "does not ship what the device loads:[ \r\n]+menu\\.room")

# Desktop binaries carry their own C++ runtime (see the root CMakeLists).
if(MINGW)
    add_test(NAME desktop_binaries_carry_their_cxx_runtime
             COMMAND ${CMAKE_COMMAND} -DOBJDUMP=${CMAKE_OBJDUMP}
                     "-DBINARIES=$<TARGET_FILE:bsht_tests>;$<TARGET_FILE:bsht_parity>;$<TARGET_FILE:bsht_desktop>;$<TARGET_FILE:bake_room>;$<TARGET_FILE:bsht-extract>"
                     -P ${CMAKE_SOURCE_DIR}/tests/check_cxx_runtime_static.cmake)
    add_test(NAME release_carries_every_exporter
             COMMAND ${CMAKE_COMMAND} -DTOOL=$<TARGET_FILE:bsht-extract> -DDIR=${CMAKE_SOURCE_DIR}/tools/godot
                     -P ${CMAKE_SOURCE_DIR}/tests/check_exporter_list.cmake)
    add_test(NAME windows_exes_use_utf8
             COMMAND ${CMAKE_COMMAND}
                     "-DBINARIES=$<TARGET_FILE:bsht_desktop>;$<TARGET_FILE:bsht-extract>"
                     -P ${CMAKE_SOURCE_DIR}/tests/check_utf8_manifest.cmake)
endif()

# The entry points stay platform-only: game code lives in app::GameApp.
add_test(NAME entry_points_stay_platform_only
         COMMAND ${CMAKE_COMMAND}
                 -DSRC=${CMAKE_SOURCE_DIR}/src
                 # A backstop: the forbidden-construct list in the script is
                 # what keeps game code out of the entry points.
                 -DMAX_LINES=400
                 -P ${CMAKE_SOURCE_DIR}/tests/check_entry_points.cmake)

# One expression of the prop-visibility rule. The order it depends on lives in
# SceneRenderer, which needs a GL context and so cannot be unit tested; what can
# be checked is that the rule lives in exactly one place.
add_test(NAME prop_visibility_has_one_rule
         COMMAND ${CMAKE_COMMAND}
                 -DSRC=${CMAKE_SOURCE_DIR}/src
                 -P ${CMAKE_SOURCE_DIR}/tests/check_prop_visibility_rule.cmake)

# The checker against real Godot. Each scenario is run by
# tools/godot/dump_state.gd to produce assets/reference/<name>.json and by
# bsht_parity against the godot:: runtime; the test is the diff. A missing
# reference skips rather than passes, so an unchecked rule is never green.
add_executable(bsht_parity
    parity/parity_main.cpp
    parity/parity_dump.cpp
    parity/probes.cpp)
target_include_directories(bsht_parity PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/tests)
target_link_libraries(bsht_parity PRIVATE bsht_game bsht_scripts bsht_godot bsht_assets bsht_core)

foreach(_scenario load_main frame_order rng anim_sample anim_play_semantics anim_main_shoot ceremony_b1r1
                  grab_shotgun_b1r1 grab_shotgun_touch_b1r1 shoot_dealer_b1r1 shoot_self_b1r1 items_b2r1
                  use_magnifier_b2r1 use_cigarettes_b2r1 use_inverter_b2r1 use_phone_b2r1
                  use_beer_b2r1 use_handsaw_b2r1
                  use_medicine_hurt_b2r1 use_medicine_heal_b2r1
                  use_handcuffs_b2r1 use_adrenaline_b2r1
                  dealer_items_b2r1 dealer_steal_b2r1 dealer_round2_b2r1 dealer_medicine_b2r1
                  menu_boot menu_options menu_start
                  intro_walk intro_revival intro_crt_boot waiver_name
                  ending_briefcase ending_true_death ending_don_score)
    add_test(NAME parity_${_scenario}
             COMMAND bsht_parity
                     ${CMAKE_SOURCE_DIR}/tests/parity/scenarios/${_scenario}.json
                     ${CMAKE_SOURCE_DIR}/assets/reference/${_scenario}.json
                     ${CMAKE_SOURCE_DIR}/assets/extracted/runtime_main.json)
    set_tests_properties(parity_${_scenario} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()
# heaven_*: RunHeaven reads runtime_heaven.json beside the runtime scene given.
foreach(_scenario heaven_fly)
    add_test(NAME parity_${_scenario}
             COMMAND bsht_parity
                     ${CMAKE_SOURCE_DIR}/tests/parity/scenarios/${_scenario}.json
                     ${CMAKE_SOURCE_DIR}/assets/reference/${_scenario}.json
                     ${CMAKE_SOURCE_DIR}/assets/extracted/runtime_main.json)
    set_tests_properties(parity_${_scenario} PROPERTIES SKIP_RETURN_CODE 77)
endforeach()

# The game the port is checked against: bsht-extract <exe> --unpack-to
# external/game-v2.2 writes it. Tests that need it skip without it.
set(BSHT_REFERENCE_GAME "${CMAKE_SOURCE_DIR}/external/game-v2.2/game" CACHE PATH
    "Unpacked reference game (bsht-extract --unpack-to)")

# Every translated script mirrors its GDScript: same functions, or an explicit
# "// PORT: omitted" line. Negative-tested against a fixture that must fail.
add_test(NAME scripts_mirror_gdscript
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_SOURCE_DIR}
                 -DGD_DIR=${BSHT_REFERENCE_GAME}/scripts
                 -P ${CMAKE_SOURCE_DIR}/tests/check_scripts_mirror.cmake)
set_tests_properties(scripts_mirror_gdscript PROPERTIES SKIP_RETURN_CODE 77)
add_test(NAME scripts_mirror_gdscript_rejects_missing_func
         COMMAND ${CMAKE_COMMAND}
                 -DROOT=${CMAKE_SOURCE_DIR}/tests/fixtures/scripts_mirror_bad
                 -DGD_DIR=${CMAKE_SOURCE_DIR}/tests/fixtures/scripts_mirror_bad/scripts
                 -P ${CMAKE_SOURCE_DIR}/tests/check_scripts_mirror.cmake)
set_tests_properties(scripts_mirror_gdscript_rejects_missing_func
    PROPERTIES PASS_REGULAR_EXPRESSION "is neither translated")

# The "gd: La-Lb" references name the lines of the reference game's scripts.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME gd_ranges_match_the_reference
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/fix_gd_ranges.py --check
                     --game ${BSHT_REFERENCE_GAME})
    set_tests_properties(gd_ranges_match_the_reference PROPERTIES SKIP_RETURN_CODE 77)
    add_test(NAME gd_ranges_rejects_a_stale_range
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/fix_gd_ranges.py --check
                     --root ${CMAKE_SOURCE_DIR}/tests/fixtures/gd_ranges_stale
                     --game ${CMAKE_SOURCE_DIR}/tests/fixtures/gd_ranges_stale/game)
    set_tests_properties(gd_ranges_rejects_a_stale_range
        PROPERTIES PASS_REGULAR_EXPRESSION "TurnOff is L8-L9, the comment says L7-L8")
else()
    message(WARNING "Python 3 not found: the gd-range guard is not registered")
endif()

# The port ships the engine; players supply the game files from a copy they
# own. Content compiled into the binary would make the vpk a redistribution of
# the game, so this fails the build. Scoped to prose: node paths and item ids
# are structural, and the port must hardcode some of them.
add_test(NAME no_game_content_in_binary
         COMMAND ${CMAKE_COMMAND}
                 -DSRC=${CMAKE_SOURCE_DIR}/src
                 -DASSETS=${CMAKE_SOURCE_DIR}/assets/extracted
                 -P ${CMAKE_SOURCE_DIR}/tests/check_no_game_content.cmake)
set_tests_properties(no_game_content_in_binary PROPERTIES SKIP_REGULAR_EXPRESSION "SKIP: needs ")
