set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ZEPHYR_SDK_INSTALL_DIR)

if(WIN32 OR MINGW OR CMAKE_HOST_WIN32)
    set(_BM_HOST_EXE ".exe")
else()
    set(_BM_HOST_EXE "")
endif()

if(DEFINED ZEPHYR_SDK_INSTALL_DIR AND EXISTS
   "${ZEPHYR_SDK_INSTALL_DIR}/riscv64-zephyr-elf/bin/riscv64-zephyr-elf-gcc${_BM_HOST_EXE}")
    set(_pfx "${ZEPHYR_SDK_INSTALL_DIR}/riscv64-zephyr-elf/bin/riscv64-zephyr-elf")
    set(CMAKE_C_COMPILER   "${_pfx}-gcc${_BM_HOST_EXE}")
    set(CMAKE_ASM_COMPILER "${_pfx}-gcc${_BM_HOST_EXE}")
    set(CMAKE_OBJCOPY      "${_pfx}-objcopy${_BM_HOST_EXE}")
else()
    find_program(_BM_RISCV32_GCC
        NAMES riscv-none-elf-gcc riscv32-unknown-elf-gcc riscv64-unknown-elf-gcc)
    if(NOT _BM_RISCV32_GCC)
        message(FATAL_ERROR
            "RISC-V 32 toolchain not found (riscv-none-elf-gcc or riscv32-unknown-elf-gcc)")
    endif()
    set(CMAKE_C_COMPILER ${_BM_RISCV32_GCC})
    set(CMAKE_ASM_COMPILER ${_BM_RISCV32_GCC})
    find_program(CMAKE_OBJCOPY NAMES riscv-none-elf-objcopy riscv32-unknown-elf-objcopy)
endif()

execute_process(
    COMMAND ${CMAKE_C_COMPILER} -dumpversion
    OUTPUT_VARIABLE _BM_RISCV_GCC_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
string(REGEX MATCH "^[0-9]+" _BM_RISCV_GCC_MAJOR "${_BM_RISCV_GCC_VERSION}")
if(_BM_RISCV_GCC_MAJOR GREATER_EQUAL 12)
    set(_BM_RISCV_MARCH "rv32imac_zicsr_zifencei")
else()
    set(_BM_RISCV_MARCH "rv32imac")
endif()

set(CMAKE_C_FLAGS "-march=${_BM_RISCV_MARCH} -mabi=ilp32 -Os -ffunction-sections -fdata-sections -Wall -Wextra -std=c99")
set(CMAKE_ASM_FLAGS "-march=${_BM_RISCV_MARCH} -mabi=ilp32")
