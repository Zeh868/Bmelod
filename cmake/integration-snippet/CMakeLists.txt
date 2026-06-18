cmake_minimum_required(VERSION 3.20)
project(bmelod_integration_smoke C)

set(BMELOD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
include(${BMELOD_ROOT}/cmake/bmelod.cmake)

bmelod_configure(
    ROOT    ${BMELOD_ROOT}
    PROFILE minimal
    BACKEND native_sim
    CONFIG  ${CMAKE_CURRENT_SOURCE_DIR}/bm_config.h
)

add_executable(bmelod_integration_smoke test_consumer.c)
bmelod_link(bmelod_integration_smoke)
