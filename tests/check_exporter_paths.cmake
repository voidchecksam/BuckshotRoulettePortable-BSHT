# Every exporter must write through SceneArgs.extracted(), so bsht-extract can
# point it at its work folder. Only scene_args.gd may name the default root.
file(GLOB _scripts "${DIR}/export_*.gd" "${DIR}/texture_export.gd")
set(_bad "")
foreach(_s ${_scripts})
    file(READ "${_s}" _text)
    if(_text MATCHES "res://assets/extracted")
        list(APPEND _bad "${_s}")
    endif()
endforeach()
if(_bad)
    message(FATAL_ERROR "write through SceneArgs.extracted() instead of res://assets/extracted: ${_bad}")
endif()
