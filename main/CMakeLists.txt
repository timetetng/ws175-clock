# 编译时刻作为无 RTC/WiFi 时的时间基准
execute_process(COMMAND date +%s OUTPUT_VARIABLE BUILD_EPOCH OUTPUT_STRIP_TRAILING_WHITESPACE)

idf_component_register(
    SRCS main.c
    INCLUDE_DIRS .)

target_compile_definitions(${COMPONENT_LIB} PRIVATE BUILD_EPOCH=${BUILD_EPOCH}LL)
if(MUSIC_SELFTEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE MUSIC_SELFTEST=1)
endif()

spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format)

idf_component_get_property(LVGL_LIB lvgl__lvgl COMPONENT_LIB)
target_compile_options(${LVGL_LIB} PRIVATE -DLV_LVGL_H_INCLUDE_SIMPLE)
