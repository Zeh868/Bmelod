# ESP-IDF 底层头文件集成（Bmelod 裸机 CMake 适配）
# 仅注入 Xtensa 工具链和低层头文件，不接入调度器或高级外设封装。

function(bm_sdk_esp32_idf_failfast_scan)
    file(GLOB_RECURSE _scan_files
        LIST_DIRECTORIES false
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf/*.S"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf/*.ld"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/boot/esp32_idf/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/boot/esp32_idf/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/boot/esp32_idf/*.S"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/boot/esp32_idf/*.ld"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/packs/sdk_esp32_idf/CMakeLists.txt"
        "${CMAKE_CURRENT_LIST_DIR}/bm_sdk_esp32_idf.cmake")

    string(CONCAT _p1 "#include \"" "dr" "iver/")
    string(CONCAT _p2 "#include <" "dr" "iver/")
    string(CONCAT _p3 "freertos/" "task")
    string(CONCAT _p4 "freertos/" "queue")
    string(CONCAT _p5 "freertos/" "semaphore")
    string(CONCAT _p6 "esp_" "timer")
    string(CONCAT _p7 "app_" "main")
    string(CONCAT _p8 "mall" "oc(")
    string(CONCAT _p9 "call" "oc(")
    string(CONCAT _p10 "real" "loc(")
    string(CONCAT _p11 "free" "(")
    string(CONCAT _p12 "pvPort" "Malloc")
    string(CONCAT _p13 "heap_caps_" "malloc")
    string(CONCAT _p14 "esp_" "event")
    string(CONCAT _p15 "esp_" "wifi")
    string(CONCAT _p16 "esp_intr_" "alloc")
    string(CONCAT _p17 "esp_task_" "wdt")
    set(_forbidden_patterns
        "${_p1}"
        "${_p2}"
        "${_p3}"
        "${_p4}"
        "${_p5}"
        "${_p6}"
        "${_p7}"
        "${_p8}"
        "${_p9}"
        "${_p10}"
        "${_p11}"
        "${_p12}"
        "${_p13}"
        "${_p14}"
        "${_p15}"
        "${_p16}"
        "${_p17}")

    foreach(_file IN LISTS _scan_files)
        if(NOT EXISTS "${_file}")
            continue()
        endif()
        file(READ "${_file}" _content)
        foreach(_pattern IN LISTS _forbidden_patterns)
            string(FIND "${_content}" "${_pattern}" _found)
            if(NOT _found EQUAL -1)
                message(FATAL_ERROR
                    "裸机后端扫描失败：${_file} 中命中禁用模式 `${_pattern}`。")
            endif()
        endforeach()
    endforeach()

    if(DEFINED ENV{IDF_PATH})
        set(_idf "$ENV{IDF_PATH}")
        set(_required_dirs
            "${_idf}/components/hal/include"
            "${_idf}/components/soc/include"
            "${_idf}/components/soc/esp32/include"
            "${_idf}/components/esp_common/include"
            "${_idf}/components/esp_hw_support/include"
            "${_idf}/components/esp_rom/include"
            "${_idf}/components/esp_system/include"
            "${_idf}/components/xtensa/include"
            "${_idf}/components/xtensa/esp32/include")
        foreach(_dir IN LISTS _required_dirs)
            if(NOT EXISTS "${_dir}")
                message(FATAL_ERROR
                    "IDF_PATH 已设置，但缺少必要的底层头目录：${_dir}")
            endif()
        endforeach()
    endif()
endfunction()

function(bm_sdk_esp32_idf_apply TARGET)
    if(NOT DEFINED ENV{IDF_PATH})
        message(FATAL_ERROR
            "IDF_PATH is required for the baremetal sdk_esp32_idf backend.\n"
            "Install ESP-IDF and export IDF_PATH, or build as an IDF component.")
    endif()

    set(_idf "$ENV{IDF_PATH}")
    set(_inc
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf"
        "${_idf}/components/hal/include"
        "${_idf}/components/soc/include"
        "${_idf}/components/soc/esp32/include"
        "${_idf}/components/hal/platform_port/include"
        "${_idf}/components/esp_common/include"
        "${_idf}/components/esp_hw_support/include"
        "${_idf}/components/esp_rom/include"
        "${_idf}/components/esp_rom/esp32/include"
        "${_idf}/components/esp_system/include"
        "${_idf}/components/log/include"
        "${_idf}/components/esp_hal_gpio/esp32/include"
        "${_idf}/components/esp_hal_i2c/esp32/include"
        "${_idf}/components/esp_hal_ana_conv/esp32/include"
        "${_idf}/components/esp_hal_mcpwm/esp32/include"
        "${_idf}/components/esp_hal_wdt/esp32/include"
        "${_idf}/components/xtensa/include"
        "${_idf}/components/xtensa/esp32/include"
        "${_idf}/components/newlib/platform_include")

    foreach(_dir IN LISTS _inc)
        if(EXISTS "${_dir}")
            target_include_directories(${TARGET} PRIVATE "${_dir}")
        endif()
    endforeach()

    bm_sdk_esp32_idf_failfast_scan()

    target_compile_definitions(${TARGET} PRIVATE
        BM_ESP32_BAREMETAL=1
        IDF_VER=\"standalone\")
endfunction()
