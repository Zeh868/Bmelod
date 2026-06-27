OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY {
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 32M
}

_stack_size_per_cpu = 8K;
_estack_cpu0 = ORIGIN(RAM) + LENGTH(RAM);
_estack_cpu1 = _estack_cpu0 - _stack_size_per_cpu;

SECTIONS {
    .text : {
        KEEP(*(.text.init))
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > RAM

    _sidata = LOADADDR(.data);

    .data : ALIGN(4) {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > RAM

    .bss : ALIGN(4) {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > RAM
}
