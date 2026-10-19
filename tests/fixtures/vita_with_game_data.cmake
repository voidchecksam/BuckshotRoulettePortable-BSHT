# check_vpk_no_game_data must refuse this: a room inside the vpk.
function(vita_package target title_id title_name)
    vita_create_vpk(${target}.vpk ${title_id} ${target}.self
        FILE "${CMAKE_SOURCE_DIR}/livearea/ICON0.PNG" sce_sys/icon0.png
        FILE "${CMAKE_SOURCE_DIR}/assets/baked/full_world.room" data/full_world.room)
endfunction()
