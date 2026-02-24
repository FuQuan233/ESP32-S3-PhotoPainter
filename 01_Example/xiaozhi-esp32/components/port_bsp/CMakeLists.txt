# 新增提示音 OGG 文件 (mode*.ogg 已由 main 组件嵌入，此处只嵌入新增的提示音)
set(PROMPT_OGG_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/ogg_src/wifi_connecting.ogg
    ${CMAKE_CURRENT_SOURCE_DIR}/ogg_src/wifi_success.ogg
    ${CMAKE_CURRENT_SOURCE_DIR}/ogg_src/wifi_fail.ogg
    ${CMAKE_CURRENT_SOURCE_DIR}/ogg_src/wifi_reset.ogg
    ${CMAKE_CURRENT_SOURCE_DIR}/ogg_src/wait_config.ogg
    ${CMAKE_CURRENT_SOURCE_DIR}/ogg_src/manual_refresh.ogg
)

idf_component_register(
    SRCS 
    "i2c_bsp.cpp" 
    "display_bsp.cpp" 
    "sdcard_bsp.cpp" 
    "./src/multi_button/multi_button.c" 
    "button_bsp.c" 
    "led_bsp.c"
    "./codec_bsp.cpp"
    "./i2c_equipment.cpp"
    "./src/fonts/font14CN.c"
    "./src/fonts/font18CN.c" 
    "./src/fonts/font22CN.c"
    "traverse_nvs.cpp"
    EMBED_FILES ${PROMPT_OGG_FILES}
    PRIV_REQUIRES 
    nvs_flash
    esp_timer
    codec_board
    78__esp-opus
    78__esp-opus-encoder
    REQUIRES
    esp_driver_sdmmc
    fatfs
    app_bsp
    REQUIRES
    driver
    INCLUDE_DIRS 
    "./" 
    "./src/multi_button"
    "./src/fonts")
set_source_files_properties(
    sdcard_bsp.cpp
    PROPERTIES
    COMPILE_OPTIONS "-Wno-format-truncation"
)

