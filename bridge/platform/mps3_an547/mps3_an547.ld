/* Linker script for the Arm MPS3 AN547 FPGA image (Cortex-M55) as modeled
 * by QEMU's mps3-an547 machine. The ELF is loaded directly into RAM by
 * QEMU's -kernel loader, so .data needs no load-time copy (VMA == LMA).
 * Ported from SampleRateTap's platform tree (shared embedded story).
 *
 * Memory map (QEMU model):
 *   ITCM   512 KB @ 0x00000000  - vector table (VTOR resets to 0)
 *   SRAM     2 MB @ 0x01000000  - code + read-only data
 *   DTCM   512 KB @ 0x20000000  - stack
 *   ISRAM    2 MB @ 0x21000000  - data + bss + heap
 */
MEMORY
{
    ITCM (rx)  : ORIGIN = 0x00000000, LENGTH = 512K
    CODE (rx)  : ORIGIN = 0x01000000, LENGTH = 2M
    DTCM (rw)  : ORIGIN = 0x20000000, LENGTH = 512K
    DATA (rw)  : ORIGIN = 0x21000000, LENGTH = 2M
}

__stack_top = ORIGIN(DTCM) + LENGTH(DTCM);
/* MSPLIM (set in Reset_Handler): the stack owns all of DTCM, so the lowest
 * address it may legally reach is the region base. */
__stack_limit = ORIGIN(DTCM);

ENTRY(Reset_Handler)

SECTIONS
{
    .vectors : {
        KEEP(*(.vectors))
    } > ITCM

    .text : {
        *(.text*)
        *(.rodata*)
        KEEP(*(.init))
        KEEP(*(.fini))
    } > CODE

    .ARM.extab : {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > CODE

    .ARM.exidx : {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > CODE

    .preinit_array : {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP(*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > CODE

    .init_array : {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > CODE

    .fini_array : {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP(*(SORT(.fini_array.*)))
        KEEP(*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > CODE

    .data : {
        *(.data*)
    } > DATA

    .bss (NOLOAD) : {
        __bss_start__ = .;
        *(.bss*)
        *(COMMON)
        __bss_end__ = .;
    } > DATA

    .heap (NOLOAD) : ALIGN(8) {
        __heap_start__ = .;
        . = ORIGIN(DATA) + LENGTH(DATA);
        __heap_end__ = .;
    } > DATA

    /* librdimon's (unused, weak) _sbrk references `end`; satisfy it. */
    PROVIDE(end = __heap_start__);
}
