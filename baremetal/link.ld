OUTPUT_ARCH(arm)
ENTRY(_start)
MEMORY { SRAM (rwx) : ORIGIN = 0x00000000, LENGTH = 16K }
SECTIONS {
    .text : { KEEP(*(.text.boot)) *(.text*) *(.rodata*) } > SRAM
    /DISCARD/ : { *(.ARM.attributes) *(.comment) *(.note*) }
    ASSERT(SIZEOF(.text) <= 8192, "Diagnostic exceeds 8 KiB image budget")
}
