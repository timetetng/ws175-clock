# 编译时刻作为无 RTC/WiFi 时的时间基准
execute_process(COMMAND date +%s OUTPUT_VARIABLE BUILD_EPOCH OUTPUT_STRIP_TRAILING_WHITESPACE)

idf_component_register(
    SRCS main.c toy_shell.c connectivity.c connectivity_model.c
    INCLUDE_DIRS .
    EMBED_FILES assets/frog-laugh.gif assets/frog-idle.rgb565 assets/frog-voice.wav)

target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_EPOCH=${BUILD_EPOCH}LL)
set(_selftests 0)
foreach(_test IN ITEMS MUSIC_SELFTEST FROG_SELFTEST SETTINGS_SELFTEST TERMINAL_SELFTEST)
    if(${_test})
        math(EXPR _selftests "${_selftests} + 1")
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${_test}=1)
    endif()
endforeach()
if(_selftests GREATER 1)
    message(FATAL_ERROR "Run only one device selftest at a time")
endif()

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(${LVGL_LIB} PRIVATE -DLV_LVGL_H_INCLUDE_SIMPLE)
