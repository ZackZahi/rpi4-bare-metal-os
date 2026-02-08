/* linker.ld - Linker script for Raspberry Pi 4 kernel */

ENTRY(_start)

SECTIONS
{
    /* Kernel loads at 0x80000 */
    . = 0x80000;
    
    /* Code section */
    .text :
    {
        KEEP(*(.text.boot))
        *(.text)
    }
    
    /* Read-only data */
    .rodata :
    {
        *(.rodata)
    }
    
    /* Initialized data */
    .data :
    {
        *(.data)
    }
    
    /* Uninitialized data (BSS) */
    .bss :
    {
        __bss_start = .;
        *(.bss)
        *(COMMON)
        __bss_end = .;
    }
    
    __bss_size = (__bss_end - __bss_start) >> 3;
}
