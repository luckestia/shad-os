/* boot.S — Point d'entrée du kernel + en-tête Multiboot
 * Syntaxe GNU AS (AT&T). Compatible avec GRUB (spec Multiboot 1).
 */

.set ALIGN,    1<<0             /* aligner les modules chargés sur 4KB */
.set MEMINFO,  1<<1             /* fournir la carte mémoire au kernel */
.set FLAGS,    ALIGN | MEMINFO  /* PAS de VIDINFO : on reste en mode texte VGA,
                                    seule la variante boot_gfx.S (shadestia-gfx.bin)
                                    demande un framebuffer graphique - voir
                                    CORRECTIONS.md pour le pourquoi de cette
                                    separation en deux binaires. */
.set MAGIC,    0x1BADB002       /* nombre magique attendu par GRUB */
.set CHECKSUM, -(MAGIC + FLAGS) /* magic + flags + checksum doit faire 0 */

/* --- En-tête Multiboot : DOIT être dans les 8 premiers Ko du binaire ---
 * Le flag "a" (alloc) est OBLIGATOIRE ici : sans lui, l'assembleur ne
 * marque pas la section comme chargeable (SHF_ALLOC), et le linker
 * l'exclut de tout segment PT_LOAD. Elle se retrouve alors repoussée
 * à la fin du fichier, bien au-delà des 8 Ko requis par la spec
 * Multiboot -> GRUB ne trouve plus l'en-tête et refuse de booter le
 * kernel (vérifiable avec `grub-file --is-x86-multiboot shadestia.bin`). */
.section .multiboot, "a"
.align 4
    .long MAGIC
    .long FLAGS
    .long CHECKSUM

/* --- Pile du kernel (16 Ko), le CPU démarre sans pile valide --- */
.section .bss
.align 16
stack_bottom:
    .skip 16384
stack_top:

/* --- Point d'entrée : GRUB saute ici en mode protégé 32 bits --- */
.section .text
.global _start
.type _start, @function
_start:
    mov $stack_top, %esp   /* initialise le pointeur de pile */

    push %ebx                /* 2e argument : pointeur vers multiboot_info */
    push %eax                /* 1er argument : nombre magique (verif optionnelle) */
    call kernel_main        /* appelle notre code C */

    /* Si kernel_main revient (ne devrait jamais arriver) : boucle infinie */
    cli
1:  hlt
    jmp 1b

.size _start, . - _start
