# ESP-IDF soc / driver 头文件集成（Bmelod 独立 CMake 构建路径）
#
# 量产推荐将 portable/vendor/esp32_idf 作为 ESP-IDF 组件编入 idf.py 工程。
# 独立 CMake 构建需设置 IDF_PATH 环境变量。

function(bm_sdk_esp32_idf_apply TARGET)
    if(DEFINED ESP_PLATFORM)
        return()
    endif()

    if(NOT DEFINED ENV{IDF_PATH})
        message(FATAL_ERROR
            "IDF_PATH is required for sdk_esp32_idf backend.\n"
            "Install ESP-IDF and export IDF_PATH, or build as an IDF component.")
    endif()

    set(_idf "$ENV{IDF_PATH}")
    set(_inc
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf"
        "${_idf}/components/soc/include"
        "${_idf}/components/soc/esp32/include"
        "${_idf}/components/hal/include"
        "${_idf}/components/hal/esp32/include"
        "${_idf}/components/hal/platform_port/include"
        "${_idf}/components/esp_common/include"
        "${_idf}/components/esp_hw_support/include"
        "${_idf}/components/esp_rom/include"
        "${_idf}/components/esp_rom/esp32/include"
        "${_idf}/components/esp_system/include"
        "${_idf}/components/log/include"
        "${_idf}/components/driver/include"
        "${_idf}/components/driver/deprecated"
        "${_idf}/components/esp_timer/include"
        "${_idf}/components/xtensa/include"
        "${_idf}/components/xtensa/esp32/include"
        "${_idf}/components/freertos/FreeRTOS-Kernel/include"
        "${_idf}/components/freertos/esp_additions/include"
        "${_idf}/components/freertos/config/include"
        "${_idf}/components/freertos/config/xtensa/include"
        "${_idf}/components/newlib/platform_include"
        "${_idf}/components/esp_pm/include"
    )

    foreach(_dir IN LISTS _inc)
        if(EXISTS "${_dir}")
            target_include_directories(${TARGET} PRIVATE "${_dir}")
        endif()
    endforeach()

    target_compile_definitions(${TARGET} PRIVATE
        ESP_PLATFORM=1
        IDF_VER=\"standalone\"
    )
    target_compile_options(${TARGET} PRIVATE
        -include "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf/bm_idf_minimal_config.h"
    )
endfunction()
