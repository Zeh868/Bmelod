# ATK-DMG474 真机冒烟工程（STM32G474VET6 + J-Link RTT）
#
# 用法见 README.md；推荐：
#   powershell tools/board/build_atk_dmg474_smoke.ps1

cmake_minimum_required(VERSION 3.20)

get_filename_component(BM_FRAMEWORK_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        "${BM_FRAMEWORK_ROOT}/cmake/toolchain-arm-none-eabi-g4.cmake"
        CACHE FILEPATH "G4 arm-none-eabi toolchain" FORCE)
endif()

project(atk_dmg474_smoke C ASM)

set(BM_BACKEND "sdk_stm32g4" CACHE STRING "" FORCE)
set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_SEGGER_RTT ON CACHE BOOL "" FORCE)
set(BM_ENABLE_ALGORITHM OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_STREAM OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_PIPELINE OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_HRT OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_EXEC OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_TICKER OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_SHELL OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_SYNC OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_MODULE OFF CACHE BOOL "" FORCE)
set(BM_ENABLE_WDG OFF CACHE BOOL "" FORCE)

# Console：LOG=RTT(3)，CLI=NONE(0)；须在 add_subdirectory 框架之前 set
set(BM_CONSOLE_LOG_BACKEND 3)
set(BM_CONSOLE_CLI_BACKEND 0)

set(BM_CONFIG_FILE
    "${CMAKE_CURRENT_LIST_DIR}/bm_config_app.h"
    CACHE FILEPATH "" FORCE)

if(NOT BM_STM32_CUBE_PATH)
    # 默认外置 SDK；可用 -DBM_STM32_CUBE_PATH= 覆盖
    set(BM_STM32_CUBE_PATH
        "D:/Code/Bmelod-sdks/stm32/STM32CubeG4"
        CACHE PATH "STM32CubeG4 root" FORCE)
endif()

set(BM_STM32_DEVICE "STM32G474xx" CACHE STRING "" FORCE)

add_subdirectory("${BM_FRAMEWORK_ROOT}" bmelod_framework)

set(ATK_SMOKE_LD "${CMAKE_CURRENT_LIST_DIR}/STM32G474VETX_FLASH.ld")
set(ATK_SMOKE_STARTUP
    "${BM_STM32_CUBE_PATH}/Drivers/CMSIS/Device/ST/STM32G4xx/Source/Templates/gcc/startup_stm32g474xx.s")
set(ATK_SMOKE_SYSTEM
    "${BM_STM32_CUBE_PATH}/Drivers/CMSIS/Device/ST/STM32G4xx/Source/Templates/system_stm32g4xx.c")

if(NOT EXISTS "${ATK_SMOKE_STARTUP}")
    message(FATAL_ERROR "Cube startup not found: ${ATK_SMOKE_STARTUP}")
endif()
if(NOT EXISTS "${ATK_SMOKE_SYSTEM}")
    message(FATAL_ERROR "Cube system_stm32g4xx.c not found: ${ATK_SMOKE_SYSTEM}")
endif()

add_executable(atk_dmg474_smoke
    main.c
    syscalls_stub.c
    ${ATK_SMOKE_STARTUP}
    ${ATK_SMOKE_SYSTEM}
)

target_include_directories(atk_dmg474_smoke PRIVATE
    ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(atk_dmg474_smoke PRIVATE
    bm_framework
    bm_hal_stm32g4)

# 框架子目录内的 BM_INCLUDE_INTERNAL_DIRS 不外泄到本工程，显式补齐短路径 include
target_include_directories(atk_dmg474_smoke PRIVATE
    "${BM_FRAMEWORK_ROOT}/include/hal"
    "${BM_FRAMEWORK_ROOT}/include/drv"
    "${BM_FRAMEWORK_ROOT}/include/bm/common"
    "${BM_FRAMEWORK_ROOT}/include/board"
    "${BM_STM32_CUBE_PATH}/Drivers/CMSIS/Device/ST/STM32G4xx/Include"
    "${BM_STM32_CUBE_PATH}/Drivers/CMSIS/Include"
    "${BM_STM32_CUBE_PATH}/Drivers/STM32G4xx_HAL_Driver/Inc")
target_compile_definitions(atk_dmg474_smoke PRIVATE
    ${BM_STM32_DEVICE}
    USE_FULL_LL_DRIVER)

target_link_options(atk_dmg474_smoke PRIVATE
    "-T${ATK_SMOKE_LD}"
    -Wl,--gc-sections
    -Wl,-Map=$<TARGET_FILE_DIR:atk_dmg474_smoke>/atk_dmg474_smoke.map
    --specs=nano.specs
    --specs=nosys.specs)

# 启动汇编声明 softvfp；应用用 hard ABI。对 startup 单文件改 soft 无冲突。
set_source_files_properties(${ATK_SMOKE_STARTUP} PROPERTIES
    COMPILE_FLAGS "-mfloat-abi=soft")

add_custom_command(TARGET atk_dmg474_smoke POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE:atk_dmg474_smoke>
            $<TARGET_FILE_DIR:atk_dmg474_smoke>/atk_dmg474_smoke.elf
    COMMAND ${CMAKE_OBJCOPY} -O binary
            $<TARGET_FILE:atk_dmg474_smoke>
            $<TARGET_FILE_DIR:atk_dmg474_smoke>/atk_dmg474_smoke.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:atk_dmg474_smoke>
    COMMENT "copy .elf + objcopy bin + size")
