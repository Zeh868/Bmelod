    .section .text.startup, "ax", @progbits
    .global _start
    .type _start, @function
_start:
    movi a1, _stack_top
    call0 bm_esp32_baremetal_runtime_start
1:
    waiti 0
    j 1b

    .size _start, . - _start
