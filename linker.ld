/* linker.ld — Définit comment assembler et charger le kernel en mémoire */

ENTRY(_start)

SECTIONS
{
    /* Les kernels Multiboot sont chargés à 1 Mo (0x100000) : au-delà des
       zones réservées (BIOS, mode réel, etc.) */
    . = 1M;

    /* En-tête Multiboot en tout premier, GRUB doit le trouver rapidement */
    .multiboot ALIGN(4K) : {
        *(.multiboot)
    }

    .text ALIGN(4K) : {
        *(.text)
        *(.text.*)
    }

    .rodata ALIGN(4K) : {
        *(.rodata)
        *(.rodata.*)
    }

    .data ALIGN(4K) : {
        *(.data)
        *(.data.*)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }

    /* Marque la fin du kernel en memoire (aligne sur 4Ko) : c'est a partir
       de cette adresse que l'allocateur dynamique (heap.c) peut construire
       son tas, dans la zone deja identity-mappee par paging.c. */
    . = ALIGN(4K);
    kernel_end = .;

    /* Sections générées par gcc/as qu'on n'utilise pas dans un kernel :
       on les rejette explicitement pour qu'elles ne perturbent pas le
       placement des sections précédentes. */
    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
        *(.got)
        *(.got.plt)
        *(.iplt)
        *(.rel.*)
    }
}
