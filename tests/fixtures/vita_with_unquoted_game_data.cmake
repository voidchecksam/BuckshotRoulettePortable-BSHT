# check_vpk_no_game_data must refuse this: a room staged from an unquoted source.
function(vita_package target title_id title_name)
    set(_room ${CMAKE_SOURCE_DIR}/assets/baked/full_world.room)
    vita_create_vpk(${target}.vpk ${title_id} ${target}.self
        FILE "${CMAKE_SOURCE_DIR}/livearea/ICON0.PNG" sce_sys/icon0.png
        FILE ${_room} data/full_world.room)
endfunction()
