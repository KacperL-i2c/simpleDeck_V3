/* =============================================================================
 *  Linker script dla STM32F103C6T6  (Cortex-M3)
 *  Flash: 32 KB @ 0x0800_0000
 *  RAM  : 10 KB @ 0x2000_0000
 *
 *  Kompatybilny z libopencm3 cm3/vector.c (reset_handler):
 *    - _stack            (początkowy SP, = szczyt RAM)
 *    - _data_loadaddr    (adres w Flash skąd kopiować .data)
 *    - _data / _edata    (RAM adres .data)
 *    - _ebss             (koniec .bss; reset_handler zeruje od końca .data
 *                         do _ebss)
 *    - __preinit_array_* / __init_array_* / __fini_array_* (C++ / ctor hooks)
 *
 *  Symbolika EXTERN(vector_table) wymusza emisję tablicy wektorów nawet jeśli
 *  żaden kod jawnie się do niej nie odwołuje.
 * ============================================================================= */

/* Wymuś emisję tablicy wektorów (libopencm3 vector.c, sekcja .vectors). */
EXTERN(vector_table)

ENTRY(reset_handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 10K
}

PROVIDE(_stack = ORIGIN(RAM) + LENGTH(RAM));
_estack = ORIGIN(RAM) + LENGTH(RAM);

SECTIONS
{
    .text : ALIGN(4)
    {
        KEEP(*(.vectors))                 /* tablica wektorów - MUSI być pierwsza */
        *(.text*)                         /* .text, .text.*, .text_*             */
        . = ALIGN(4);
        *(.rodata*)                       /* stałe, stringi                      */
        . = ALIGN(4);
    } > FLASH

    /* C++ static constructors / __attribute__((constructor)).
     * Wymagane przez reset_handler w libopencm3 (iteruje po tych sekcjach). */
    .preinit_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__preinit_array_start = .);
        KEEP (*(.preinit_array*))
        PROVIDE_HIDDEN(__preinit_array_end = .);
    } > FLASH
    .init_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__init_array_start = .);
        KEEP (*(SORT(.init_array.*)))
        KEEP (*(.init_array*))
        PROVIDE_HIDDEN(__init_array_end = .);
    } > FLASH
    .fini_array : ALIGN(4)
    {
        PROVIDE_HIDDEN(__fini_array_start = .);
        KEEP (*(SORT(.fini_array.*)))
        KEEP (*(.fini_array*))
        PROVIDE_HIDDEN(__fini_array_end = .);
    } > FLASH

    /* ARM exception unwinding tables */
    .ARM.extab : ALIGN(4) { *(.ARM.extab*) } > FLASH
    .ARM.exidx : ALIGN(4) {
        __exidx_start = .;
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
        __exidx_end = .;
    } > FLASH

    . = ALIGN(4);
    _etext = .;

    /* RAM ale nie zerowany przy resecie (np. boot/app comms). */
    .noinit (NOLOAD) : ALIGN(4) { *(.noinit*) } > RAM
    . = ALIGN(4);

    /* ---- .data (ładowane z Flash @ _data_loadaddr, runtime w RAM) ---- */
    .data : ALIGN(4)
    {
        _data  = .;
        _sdata = .;
        *(.data*)
        *(.ramtext*)
        . = ALIGN(4);
        _edata = .;
    } > RAM AT > FLASH
    _data_loadaddr = LOADADDR(.data);

    /* ---- .bss (zero-inicjowane) ----
     * reset_handler zeruje od końca .data aż do _ebss. */
    .bss (NOLOAD) : ALIGN(4)
    {
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
        __bss_end__ = .;
    } > RAM

    /* ---- Stos (reszta RAMa, nie inicjalizowany statycznie) ---- */
    .stack_dummy (NOLOAD) : ALIGN(8)
    {
        __stack_limit = .;
        . += 0x400;
        . = ALIGN(8);
    } > RAM
    _stack_bottom = ORIGIN(RAM);

    /DISCARD/ :
    {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
