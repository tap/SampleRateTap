/* Linker script for the Arm MPS2+ AN505 FPGA image (Cortex-M33) as modeled
 * by QEMU's mps2-an505 machine. The board boots secure with the initial
 * vector table at the secure alias 0x1000_0000, so everything is placed in
 * the secure aliases. QEMU's -kernel loader places the ELF directly into
 * RAM (VMA == LMA, no load-time copy).
 * Ported from SampleRateTap's platform tree (shared embedded story).
 *
 * Memory map (QEMU model, secure aliases):
 *   SSRAM1   4 MB @ 0x10000000  - vector table + code + rodata
 *   SSRAM2/3 4 MB @ 0x38000000  - data + bss + heap + stack
 */
MEMORY
{
    CODE (rx) : ORIGIN = 0x10000000, LENGTH = 4M
    DATA (rw) : ORIGIN = 0x38000000, LENGTH = 4M
}

__stack_top = ORIGIN(DATA) + LENGTH(DATA);

ENTRY(Reset_Handler)

SECTIONS
{
    .vectors : {
        KEEP(*(.vectors))
    } > CODE

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

    /* Stack lives at the top of DATA; cap the heap 64 KB below it. */
    .heap (NOLOAD) : ALIGN(8) {
        __heap_start__ = .;
        . = ORIGIN(DATA) + LENGTH(DATA) - 64K;
        __heap_end__ = .;
    } > DATA

    /* MSPLIM (set in Reset_Handler): the stack may descend to the heap cap
     * but no further — overflow into the heap faults instead of corrupting. */
    __stack_limit = __heap_end__;

    /* librdimon's (unused, weak) _sbrk references `end`; satisfy it. */
    PROVIDE(end = __heap_start__);
}
