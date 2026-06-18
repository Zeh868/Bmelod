# ultrasonic_frontend：合成回波块流 + ToF / 同步检波（K2）
cmake_minimum_required(VERSION 3.20)

if(NOT BM_DEMO_UNIFIED_BUILD)
    option(NATIVE_SIM_BUILD "Build for host native_sim" OFF)
endif()

set(BM_ENABLE_ALGORITHM ON CACHE BOOL "" FORCE)

if(BM_DEMO_NATIVE_SIM OR NATIVE_SIM_BUILD)
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(ultrasonic_frontend_native C)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/native_sim_example.cmake)
    file(GLOB US_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_native_sim_example(ultrasonic_frontend
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec bm_algorithm
        EXTRA_SOURCES ${US_MODULES}
    )
else()
    if(NOT BM_DEMO_UNIFIED_BUILD)
        project(ultrasonic_frontend C ASM)
    endif()
    set(EXAMPLE_ENABLE_MODULE ON)
    set(EXAMPLE_ENABLE_HRT ON)
    set(EXAMPLE_ENABLE_TICKER ON)
    set(EXAMPLE_ENABLE_EXEC ON)
    include(../cmake/qemu_example.cmake)
    file(GLOB US_MODULES CONFIGURE_DEPENDS modules/*.c)
    bm_add_qemu_example(ultrasonic_frontend
        FRAMEWORK_LIBS bm_core bm_module bm_hrt bm_ticker bm_exec bm_algorithm
        EXTRA_SOURCES ${US_MODULES}
    )
    target_link_libraries(ultrasonic_frontend.elf PRIVATE m)
endif()
