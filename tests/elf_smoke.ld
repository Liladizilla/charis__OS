ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
}

SECTIONS
{
    /* PML4 slot 1 is outside the kernel's low identity-map subtree.
     * The ELF loader maps this range privately with USER permissions. */
    . = 0x0000008000000000;
    .text : { *(.text*) *(.rodata*) } :text
}
