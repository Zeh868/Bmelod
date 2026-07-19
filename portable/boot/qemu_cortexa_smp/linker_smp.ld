OUTPUT_ARCH(arm)
ENTRY(_start)

MEMORY {
    RAM (rwx) : ORIGIN = 0x40000000, LENGTH = 128M
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_secondary_stack_size_per_cpu = 0x4000;
_secondary_stack_max_cpus = 8;

SECTIONS {
    .text : {
        KEEP(*(.text.init))
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
        . += _secondary_stack_size_per_cpu * _secondary_stack_max_cpus;
        . = ALIGN(16);
    } > RAM
}
