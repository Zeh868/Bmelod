# MP 扩展工业算法库 bmp_algo（封装开源 bm_algorithm K0）

if(TARGET bmp_algo)
    return()
endif()

if(NOT TARGET bm_algorithm)
    message(FATAL_ERROR
        "bmp_algo: bm_algorithm required; set BM_ENABLE_ALGORITHM=ON before "
        "add_subdirectory(Source/algo)")
endif()

add_library(bmp_algo STATIC
    bmp_algo_fft.c
    bmp_algo_vibration.c
    bmp_algo_bms.c
    bmp_algo_motor.c
    bmp_algo_motor_ekf.c
    bmp_algo_audio.c
)
target_include_directories(bmp_algo PUBLIC
    ${BM_FRAMEWORK_ROOT}/include)
target_compile_options(bmp_algo PRIVATE
    "$<$<C_COMPILER_ID:MSVC>:/utf-8>")
target_link_libraries(bmp_algo PUBLIC bm_config bm_algorithm)
