OUTPUT_ARCH(xtensa)
ENTRY(_start)

MEMORY {
    IRAM (rwx) : ORIGIN = 0x40080000, LENGTH = 128K
    DRAM (rw)  : ORIGIN = 0x3FFB0000, LENGTH = 176K
}

_stack_size_per_cpu = 4K;
_estack_cpu0 = ORIGIN(DRAM) + LENGTH(DRAM);
_estack_cpu1 = _estack_cpu0 - _stack_size_per_cpu;

SECTIONS {
    .iram1.vectors : ALIGN(0x100) {
        KEEP(*(.iram1.vectors))
        . = ALIGN(4);
    } > IRAM

    .iram1.text : {
        KEEP(*(.iram1.text))
        . = ALIGN(4);
    } > IRAM

    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
    } > IRAM

    _sidata = LOADADDR(.data);

    .data : ALIGN(4) {
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > DRAM AT > IRAM

    .bss : ALIGN(4) {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > DRAM
}
