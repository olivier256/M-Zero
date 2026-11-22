/* Édition de liens du banc sh2cycles, cache en mode « 2 voies + RAM »
   (option CACHE=ram de run.sh) : comme bench.ld, mais le code de
   mode7.c est placé dans la RAM interne du SH2, en C000 0000h (2 Ko au
   plus). Sur la console, le programme le recopierait là au démarrage,
   après avoir mis le cache en mode 2 voies (CCR) ; sous qemu, le segment
   y est chargé directement, mais qemu (mode utilisateur) n'accepte pas
   d'adresse au-delà de 2 Go : run.sh passe --defsym RAM_BASE=0x50000000,
   et le greffon traite cette adresse comme la RAM interne (option
   ram=0x50000000). Le temps simulé est le même. */

RAM_BASE = DEFINED(RAM_BASE) ? RAM_BASE : 0xC0000000;

ENTRY(_start)

PHDRS
{
    sdram PT_LOAD;
    marker PT_LOAD;
    fb PT_LOAD;
    ram PT_LOAD;
}

SECTIONS
{
    . = 0x06000000;
    .text : { *(EXCLUDE_FILE(*mode7.o) .text .text.*) } :sdram
    .rodata : { *(.rodata .rodata.*) } :sdram
    .data : { *(.data .data.*) } :sdram
    .bss : { *(.bss .bss.*) *(COMMON) } :sdram
    . = ALIGN(16);
    .stack : { . += 0x2000; bench_stack_top = .; } :sdram
    bench_sdram_end = .;

    . = 0x20000000;
    .marker : { *(.marker) } :marker

    . = 0x24000000;
    .fb : { *(.fb) } :fb

    . = RAM_BASE;
    .ram : { *mode7.o(.text .text.*) } :ram
    ASSERT(SIZEOF(.ram) <= 0x800, "le code en RAM interne depasse 2 Ko")
}
