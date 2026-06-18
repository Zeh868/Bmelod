    .section .text.init
    .globl _start
    .type _start, @function
_start:
    la sp, _estack
    call SystemInit
    call main
1:  j 1b
