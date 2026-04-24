/* linker.ld - Linker script for CharisOS kernel */

/* Kernel is loaded at 1 MiB (0x100000) */
ENTRY(_start)

SECTIONS
{
    /* Load kernel at 1 MiB */
    . = 0x100000;

    /* Read-only code and data */
    .text : {
        *(.text)
        *(.rodata)
    }

    .data : {
        *(.data)
    }

    .bss : {
        *(.bss)
        *(COMMON)
    }

    /* End of kernel - used for memory detection */
    _end = .;
}