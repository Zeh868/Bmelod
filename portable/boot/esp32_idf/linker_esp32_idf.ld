ENTRY(_start)

MEMORY
{
    iram0_0_seg (rx) : ORIGIN = 0x40080000, LENGTH = 0x20000
    dram0_0_seg (rw) : ORIGIN = 0x3FFB0000, LENGTH = 0x50000
}

SECTIONS
{
    .text : ALIGN(4)
    {
        _stext = .;
        KEEP(*(.text.startup))
        *(.literal .literal.*)
        *(.text .text.*)
        *(.rodata .rodata.*)
        _etext = .;
    } > iram0_0_seg

    .data : ALIGN(4)
    {
        _sdata = .;
        *(.data .data.*)
        _edata = .;
    } > dram0_0_seg AT > iram0_0_seg
    _sidata = LOADADDR(.data);

    .bss (NOLOAD) : ALIGN(4)
    {
        _sbss = .;
        *(.bss .bss.*)
        *(COMMON)
        _ebss = .;
    } > dram0_0_seg

    _stack_top = ORIGIN(dram0_0_seg) + LENGTH(dram0_0_seg);
}
