cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

set(EXAMPLE_ENABLE_MODULE ON)
set(EXAMPLE_ENABLE_SHELL OFF)
set(EXAMPLE_ENABLE_WDG OFF)

file(GLOB CORE_SENSOR_MODULES CONFIGURE_DEPENDS modules/*.c)

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(core_sensor_native C)
    endif()
    include(../cmake/native_sim_example.cmake)
    bm_add_native_sim_example(core_sensor
        FRAMEWORK_LIBS bm_core bm_module
        EXTRA_SOURCES ${CORE_SENSOR_MODULES}
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(core_sensor C ASM)
    endif()
    include(../cmake/qemu_example.cmake)
    bm_add_qemu_example(core_sensor
        FRAMEWORK_LIBS bm_core bm_module
        EXTRA_SOURCES ${CORE_SENSOR_MODULES}
    )
endif()
