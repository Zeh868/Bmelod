# configure 期负例：非法相位装配必须编译失败，且失败原因是 static assert（而非缺头）
try_compile(_let_neg_ok ${CMAKE_BINARY_DIR}/let_neg
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/let_static_assert_bad.c
    CMAKE_FLAGS "-DINCLUDE_DIRECTORIES=${BM_LET_NEG_INCLUDE_DIRS}"
    C_STANDARD 11
    OUTPUT_VARIABLE _let_neg_out)
if(_let_neg_ok)
    message(FATAL_ERROR "check_let_static_assert: 非法装配（at>=every）竟然编译通过——静态断言失效")
endif()
if(NOT _let_neg_out MATCHES "at must be < every")
    message(FATAL_ERROR "check_let_static_assert: 编译失败但不是静态断言导致（疑似 include 配错）：\n${_let_neg_out}")
endif()
message(STATUS "check_let_static_assert: 负例按预期被静态断言拦截 ✓")
