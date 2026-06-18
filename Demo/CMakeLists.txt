cmake_minimum_required(VERSION 3.20)

# 统一 Demo 构建入口：框架（bmelod）只配置/编译一次，各示例共享中间产物。
#   cmake -B build/demo/qemu -S Demo -G "MinGW Makefiles" \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
#   cmake --build build/demo/qemu --target core_sensor.elf
#
# PC native_sim：
#   cmake -B build/demo/native -S Demo -DBMELOD_DEMO_NATIVE=ON
#   cmake --build build/demo/native --target core_sensor

option(BMELOD_DEMO_NATIVE "Build all demos for host native_sim (default: QEMU)" OFF)

set(BM_DEMO_UNIFIED_BUILD ON CACHE INTERNAL "Demo umbrella build" FORCE)
if(BMELOD_DEMO_NATIVE)
    project(bmelod_demos_native C)
    set(BM_DEMO_NATIVE_SIM ON CACHE INTERNAL "" FORCE)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/native_sim_example.cmake)
    bm_demo_ensure_native_sim()
else()
    project(bmelod_demos_qemu C ASM)
    set(BM_DEMO_NATIVE_SIM OFF CACHE INTERNAL "" FORCE)
    include(${CMAKE_CURRENT_LIST_DIR}/cmake/qemu_example.cmake)
    bm_demo_ensure_qemu()
endif()

set(_demo_examples_list "${CMAKE_CURRENT_LIST_DIR}/../tools/demo/examples.txt")
if(NOT EXISTS "${_demo_examples_list}")
    message(FATAL_ERROR "examples list not found: ${_demo_examples_list}")
endif()

file(STRINGS "${_demo_examples_list}" _demo_example_names)
foreach(_demo_name IN LISTS _demo_example_names)
    if(_demo_name STREQUAL "" OR _demo_name MATCHES "^#")
        continue()
    endif()
    string(STRIP "${_demo_name}" _demo_name)
    if(NOT IS_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/${_demo_name}")
        message(FATAL_ERROR "Demo example directory missing: ${_demo_name}")
    endif()
    add_subdirectory("${_demo_name}")
endforeach()
