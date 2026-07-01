# SPDX-License-Identifier: GPL-3.0-or-later
#
# @file check_no_alloc_symbols.cmake
# @brief 零动态分配符号级机械检查：扫描静态库产物的外部符号引用表，
#        命中 malloc/calloc/realloc/free 任一符号即判定失败。
#
# @details 独立 cmake -P 脚本，供 add_test(... COMMAND ${CMAKE_COMMAND} -P ...)
# 直接调用（不依赖 shell，规避 MSYS/Git-Bash 对 "/xxx" 形式参数的路径重写）。
# MSVC 下用 dumpbin /symbols 扫描 .lib 归档里所有成员目标文件的 COFF 符号表，
# 只认定同时满足「所在行含 UNDEF（未定义外部引用）」且「符号名整词匹配」的
# 命中——避免误伤形如 bm_mempool_alloc 的子串。GNU 下用 nm 扫描 .a 归档，
# 未定义符号行形如 "                 U malloc"，同样按整词匹配。
#
# 用法：
#   cmake -DTOOL_ID=MSVC -DDUMPBIN_EXE=<dumpbin路径> -DLIB_PATH=<产物路径> \
#         -P check_no_alloc_symbols.cmake
#   cmake -DTOOL_ID=GNU  -DNM_EXE=<nm路径>            -DLIB_PATH=<产物路径> \
#         -P check_no_alloc_symbols.cmake
#
# @author zeh (china_qzh@163.com)
# @date 2026-07-01

if(NOT DEFINED TOOL_ID OR NOT DEFINED LIB_PATH)
    message(FATAL_ERROR "check_no_alloc_symbols.cmake 需要 -DTOOL_ID=... -DLIB_PATH=...")
endif()

if(TOOL_ID STREQUAL "MSVC")
    if(NOT DEFINED DUMPBIN_EXE)
        message(FATAL_ERROR "TOOL_ID=MSVC 需要 -DDUMPBIN_EXE=<dumpbin 可执行文件路径>")
    endif()
    execute_process(
        COMMAND "${DUMPBIN_EXE}" /symbols "${LIB_PATH}"
        OUTPUT_VARIABLE _sym_out
        ERROR_VARIABLE  _sym_err
        RESULT_VARIABLE _sym_rc)
elseif(TOOL_ID STREQUAL "GNU")
    if(NOT DEFINED NM_EXE)
        message(FATAL_ERROR "TOOL_ID=GNU 需要 -DNM_EXE=<nm 可执行文件路径>")
    endif()
    execute_process(
        COMMAND "${NM_EXE}" "${LIB_PATH}"
        OUTPUT_VARIABLE _sym_out
        ERROR_VARIABLE  _sym_err
        RESULT_VARIABLE _sym_rc)
else()
    message(FATAL_ERROR "未知 TOOL_ID=${TOOL_ID}（仅支持 MSVC / GNU）")
endif()

if(NOT _sym_rc EQUAL 0)
    message(FATAL_ERROR "符号表工具执行失败（rc=${_sym_rc}）：${_sym_err}")
endif()

string(REPLACE "\r\n" "\n" _sym_out "${_sym_out}")
string(REPLACE "\n" ";" _sym_lines "${_sym_out}")

set(_forbidden malloc calloc realloc free)
set(_hits "")

foreach(_line IN LISTS _sym_lines)
    foreach(_sym IN LISTS _forbidden)
        if(TOOL_ID STREQUAL "MSVC")
            # dumpbin /symbols：未定义外部符号所在行含 "UNDEF" 且符号名整词出现；
            # 动态 CRT（/MD /MDd）下 malloc/free 等以导入桩 "__imp_malloc" 形式
            # 出现（前有 __imp_ 前缀、故不能要求前一字符是非单词字符），故与
            # 直接符号两种形态分别匹配，任一命中即算。
            if(_line MATCHES "UNDEF" AND
               (_line MATCHES "([^A-Za-z0-9_]|^)${_sym}([^A-Za-z0-9_]|$)" OR
                _line MATCHES "__imp_${_sym}([^A-Za-z0-9_]|$)"))
                list(APPEND _hits "${_line}")
            endif()
        else()
            # nm：未定义符号行形如 "                 U malloc"
            if(_line MATCHES "^[0-9a-fA-F]*[ \t]+U[ \t]+${_sym}([ \t]|$)")
                list(APPEND _hits "${_line}")
            endif()
        endif()
    endforeach()
endforeach()

if(_hits)
    string(REPLACE ";" "\n  " _hits_str "${_hits}")
    message(FATAL_ERROR
        "检测到 bm_tt_schedule 产物引用动态分配符号（违反零分配设计）：\n  ${_hits_str}")
endif()

message(STATUS "bm_tt_schedule 符号表零分配检查通过：${LIB_PATH}")
