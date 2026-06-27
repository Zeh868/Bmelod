# run_qemu_tap.cmake —— 在 QEMU 中运行一个裸机 TAP 测试 ELF 并判定结果
#
# 由 ctest 以脚本模式调用：
#   cmake -DQEMU=<qemu-system-xxx> -DQEMU_ARGS=<以;分隔的参数列表>
#         -DELF=<elf路径> -DSERIAL=<串口输出文件> [-DTIMEOUT=<秒>]
#         -P run_qemu_tap.cmake
#
# 约定：被测 ELF 通过串口输出 TAP（TAP version 13 / "ok N" / "not ok N"），
# 测试结束后自行退出（cortexa 走 PSCI SYSTEM_OFF）。本脚本：
#   1. 删除旧串口文件，运行 QEMU（带超时兜底，防止次核未引导时挂死）；
#   2. 读取串口输出，统计 ok / not ok；
#   3. 出现 "not ok" 或一条 ok 都没有 → FATAL_ERROR（ctest 判 fail）；
#   4. 否则打印 "TAP PASS: N ok" 成功返回。

if(NOT DEFINED TIMEOUT)
    set(TIMEOUT 30)
endif()

if(EXISTS "${SERIAL}")
    file(REMOVE "${SERIAL}")
endif()

string(REPLACE ";" " " _args_show "${QEMU_ARGS}")
message(STATUS "QEMU: ${QEMU} ${_args_show} -serial file:${SERIAL} -kernel ${ELF}")

execute_process(
    COMMAND "${QEMU}" ${QEMU_ARGS}
            -serial "file:${SERIAL}"
            -kernel "${ELF}"
    TIMEOUT ${TIMEOUT}
    RESULT_VARIABLE _qemu_rc
    OUTPUT_VARIABLE _qemu_out
    ERROR_VARIABLE  _qemu_err)

if(NOT EXISTS "${SERIAL}")
    message(FATAL_ERROR
        "QEMU 未产生串口输出文件 (rc=${_qemu_rc}). stderr: ${_qemu_err}")
endif()

file(READ "${SERIAL}" _tap)
message(STATUS "----- 串口 TAP 输出 -----\n${_tap}\n-------------------------")

# 统计 not ok（失败）。注意 "not ok" 必须先于 "ok" 判定。
string(REGEX MATCHALL "\nnot ok |^not ok " _fails "\n${_tap}")
list(LENGTH _fails _nfail)
string(REGEX MATCHALL "\nok |^ok " _oks "\n${_tap}")
list(LENGTH _oks _npass)

if(_nfail GREATER 0)
    message(FATAL_ERROR "TAP FAIL: ${_nfail} 条 'not ok'（pass=${_npass}）")
endif()

if(_npass EQUAL 0)
    message(FATAL_ERROR
        "TAP 无任何 'ok' 行（疑似次核未引导或提前挂死，rc=${_qemu_rc}）")
endif()

message(STATUS "TAP PASS: ${_npass} ok, 0 not ok")
