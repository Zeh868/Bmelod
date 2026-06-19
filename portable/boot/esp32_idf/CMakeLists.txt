# ESP32 裸机启动骨架
#
# 这里只提供启动/链接模板，不替代普通 ESP-IDF FreeRTOS 应用入口。

add_library(bm_boot_esp32_idf STATIC
    crt0_esp32_idf.c
    startup_esp32_idf.S
)

target_include_directories(bm_boot_esp32_idf PUBLIC
    ${CMAKE_CURRENT_LIST_DIR})
