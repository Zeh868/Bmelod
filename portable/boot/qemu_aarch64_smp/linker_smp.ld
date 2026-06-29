OUTPUT_ARCH(aarch64)
ENTRY(_start)

MEMORY {
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 128M
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_secondary_stack_size = 0x4000;

SECTIONS {
    .text : {
        KEEP(*(.text.init))
        KEEP(*(.text.vectors))
        *(.text*)
        *(.rodata*)
        . = ALIGN(16);
    } > RAM

    _sidata = LOADADDR(.data);

    .data : ALIGN(16) {
        _sdata = .;
        *(.data*)
        . = ALIGN(16);
        _edata = .;
    } > RAM AT > RAM

    .bss : ALIGN(16) {
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(16);
        _ebss = .;
        _secondary_stack = .;
        . += _secondary_stack_size;
        . = ALIGN(16);
    } > RAM
}
