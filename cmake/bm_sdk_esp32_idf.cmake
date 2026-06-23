# ESP-IDF 底层头文件集成（Bmelod 裸机 CMake 适配）
# 仅注入 Xtensa 工具链和低层头文件，不接入调度器或高级外设封装。

function(bm_sdk_esp32_idf_failfast_scan)
    # Phase 3 说明：portable/boot/esp32_idf/ 已删除（IDF 路线不需要裸机 boot 骨架），
    # 此处不再扫描该目录。
    file(GLOB_RECURSE _scan_files
        LIST_DIRECTORIES false
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf/*.S"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf/*.ld"
        "${CMAKE_CURRENT_LIST_DIR}/../portable/packs/sdk_esp32_idf/CMakeLists.txt"
        "${CMAKE_CURRENT_LIST_DIR}/bm_sdk_esp32_idf.cmake")

    # Phase 2 说明：
    #   - esp_intr_alloc（_p16）已从禁用列表移除：MCPWM/timer ISR 驱动依赖此 API。
    #   - freertos/task/queue/semaphore（_p3/_p4/_p5）已移除：允许包含 IDF 中间头，
    #     但 vendor 驱动本身不得依赖 RTOS 调度（ISR 内禁止任务操作）。
    #   - esp_timer（_p6）已移除：timer_ll 直接操作 TIMERG0，但 esp_timer 头
    #     可能作为传递依赖被 IDF 内部头包含。
    #   - esp_event（_p14）已移除：不再禁止（驱动层未使用，但不应 FATAL）。
    #   - malloc/free/heap（_p8/_p9/_p10/_p11/_p12/_p13）保留：ISR 内禁止动态分配。
    #   - driver/ 高级驱动层（_p1/_p2）保留禁用。
    #   - app_main（_p7）、esp_wifi（_p15）保留禁用。
    #   - esp_task_wdt（_p17）已移出禁用列表（Phase 3）：singleton 中引入
    #     esp_task_wdt_config_t 类型定义（条件编译，裸机路径为本地 typedef）；
    #     实际 esp_task_wdt_init/reset 等函数仍不得在裸机路径下调用。
    string(CONCAT _p1 "#include \"" "dr" "iver/")
    string(CONCAT _p2 "#include <" "dr" "iver/")
    string(CONCAT _p7 "app_" "main")
    string(CONCAT _p8 "mall" "oc(")
    string(CONCAT _p9 "call" "oc(")
    string(CONCAT _p10 "real" "loc(")
    string(CONCAT _p11 "free" "(")
    string(CONCAT _p12 "pvPort" "Malloc")
    string(CONCAT _p13 "heap_caps_" "malloc")
    string(CONCAT _p15 "esp_" "wifi")
    set(_forbidden_patterns
        "${_p1}"
        "${_p2}"
        "${_p7}"
        "${_p8}"
        "${_p9}"
        "${_p10}"
        "${_p11}"
        "${_p12}"
        "${_p13}"
        "${_p15}")

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
        # 核实 IDF 5.2.3 真实存在的必要头目录
        set(_required_dirs
            "${_idf}/components/hal/platform_port/include"
            "${_idf}/components/hal/include"
            "${_idf}/components/hal/esp32/include"
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
    # IDF 5.2.3 真实头目录（已核实存在）。
    # 注意：esp_hal_gpio/i2c/ana_conv/mcpwm/wdt 是 IDF 5.4+ 拆分组件，5.2.3 不存在，已删除。
    # hal/esp32/include 包含 adc_ll.h / gpio_ll.h / mwdt_ll.h 等 LL 头。
    # esp_rom/include/esp32 是 5.2.3 下 esp32 专属 rom 头的正确路径。
    set(_inc
        "${CMAKE_CURRENT_LIST_DIR}/../portable/vendor/esp32_idf"
        "${_idf}/components/hal/platform_port/include"
        "${_idf}/components/hal/include"
        "${_idf}/components/hal/esp32/include"
        "${_idf}/components/soc/include"
        "${_idf}/components/soc/esp32/include"
        "${_idf}/components/esp_common/include"
        "${_idf}/components/esp_hw_support/include"
        "${_idf}/components/esp_rom/include"
        "${_idf}/components/esp_rom/include/esp32"
        "${_idf}/components/esp_system/include"
        "${_idf}/components/log/include"
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
