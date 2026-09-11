idf_component_register(
    SRCS
        "app_main.cpp"
        "local_room_ui.cpp"
        "lv_font_folotoy_cn_18.c"
        "passport_board.cpp"
        "turn_tone.cpp"
        "folotoy_local_room.cc"
        "folotoy_uno_duel.cc"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_gpio
        esp_driver_i2c
        esp_driver_i2s
        esp_driver_spi
        esp_event
        esp_lcd
        esp_netif
        esp_timer
        esp_wifi
        nvs_flash
        espressif__button
        espressif__esp_codec_dev
        espressif__esp_lvgl_port
        lvgl__lvgl
)

target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)