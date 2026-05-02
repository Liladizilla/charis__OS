/* linker.ld - Linker script for CharisOS kernel */

/* Kernel is loaded at 1 MiB (0x100000) */
ENTRY(start)

PHDRS {
    text   PT_LOAD FLAGS(5);   /* R-X */
    rodata PT_LOAD FLAGS(4);   /* R-- */
    data   PT_LOAD FLAGS(6);   /* RW- */
}

SECTIONS
{
    /* Load kernel at 1 MiB */
    . = 0x100000;

    /* Multiboot header must be at the beginning of the kernel and aligned */
    .multiboot : {
        KEEP(*(.multiboot))
        . = ALIGN(8);
    } :text

    /* Read-only code */
    _text_start = .;
    .text : {
        *(.text)
    } :text
    _text_end = .;

    /* Read-only data */
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    } :rodata

    .data : {
        *(.data)
    } :data

    .bss : {
        *(.bss)
        *(COMMON)
    } :data

    /* End of kernel - used for memory detection */
    _end = .;
    _kernel_end = .;
}