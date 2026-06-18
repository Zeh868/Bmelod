# 交叉编译 Bmelod 静态库（库本体，不含 Port）
# 文档：docs/03-构建与工具链/02-预编译静态库.md

cmake_minimum_required(VERSION 3.20)
project(bmelod-static-libs C)

set(BMELOD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(BMELOD_PROFILE "event" CACHE STRING "minimal | event | servo | full")
set(BMELOD_CONFIG "${BMELOD_ROOT}/cmake/integration-snippet/bm_config.h"
    CACHE FILEPATH "bm_config.h for library build")

include(${BMELOD_ROOT}/cmake/bmelod_library.cmake)
bmelod_build_libraries(
    ROOT    ${BMELOD_ROOT}
    PROFILE ${BMELOD_PROFILE}
    CONFIG  ${BMELOD_CONFIG}
)

bmelod_library_targets(_lib_targets)
message(STATUS "Bmelod static libraries: ${_lib_targets}")
message(STATUS "See docs/03-构建与工具链/02-预编译静态库.md")
