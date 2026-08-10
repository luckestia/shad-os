/* gdt.c — Global Descriptor Table : definit les segments memoire.
 * On utilise un modele "flat" (plat) : segments code/donnees noyau ET
 * utilisateur couvrant tout l'espace 0-4GB (la separation de privileges
 * vient du champ DPL du descripteur, pas d'une plage d'adresses separee),
 * plus un descripteur de TSS necessaire pour que le CPU sache OU trouver
 * une pile noyau valide quand une interruption survient pendant que du
 * code utilisateur (ring 3) est en cours d'execution.
 */
#include "kernel.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Structure attendue par le CPU pour le "task switch" materiel. On ne
   l'utilise pas pour changer de tache (notre multitache est logiciel,
   voir tasking.c) : seuls les champs esp0/ss0 nous interessent, pour
   indiquer au CPU quelle pile noyau charger automatiquement lors d'un
   changement de privilege ring3 -> ring0 (interruption pendant du code
   utilisateur). */
struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, iomap_base;
} __attribute__((packed));

#define GDT_ENTRIES 6
static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gdtp;
static struct tss_entry tss;

/* Pile noyau dediee utilisee UNIQUEMENT comme point d'atterrissage pour
   les interruptions survenant pendant l'execution de code ring3 (voir
   usermode.c). Independante des piles des taches de tasking.c. */
#define SYSCALL_KSTACK_SIZE 8192
static uint8_t syscall_kernel_stack[SYSCALL_KSTACK_SIZE] __attribute__((aligned(16)));

/* Definies en assembleur (low_level.S) : charge le registre GDTR puis
   recharge les segments (necessite un far jump, impossible en C pur). */
extern void gdt_flush(uint32_t gdtp_addr);
extern void tss_flush(void);

static void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                          uint8_t access, uint8_t gran) {
    gdt[num].base_low    = base & 0xFFFF;
    gdt[num].base_middle  = (base >> 16) & 0xFF;
    gdt[num].base_high    = (base >> 24) & 0xFF;
    gdt[num].limit_low    = limit & 0xFFFF;
    gdt[num].granularity  = (limit >> 16) & 0x0F;
    gdt[num].granularity |= gran & 0xF0;
    gdt[num].access       = access;
}

void gdt_init(void) {
    gdtp.limit = sizeof(struct gdt_entry) * GDT_ENTRIES - 1;
    gdtp.base  = (uint32_t) &gdt;

    gdt_set_gate(0, 0, 0, 0, 0);                 /* descripteur nul obligatoire */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);   /* 0x08 code noyau   (ring0) */
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF);   /* 0x10 donnees noyau (ring0) */
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);   /* 0x18 code utilisateur (ring3) */
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);   /* 0x20 donnees utilisateur (ring3) */

    /* 0x28 : descripteur de TSS. access=0x89 (present, DPL0, type "TSS 32
       bits disponible"). Granularite en octets (pas en pages) car le TSS
       fait moins de 64 Ko. */
    uint32_t tss_base  = (uint32_t) &tss;
    uint32_t tss_limit = sizeof(struct tss_entry) - 1;
    gdt_set_gate(5, tss_base, tss_limit, 0x89, 0x00);

    for (size_t i = 0; i < sizeof(struct tss_entry); i++) ((uint8_t*)&tss)[i] = 0;
    tss.ss0  = 0x10;   /* segment de pile noyau utilise lors d'un changement de privilege */
    tss.esp0 = (uint32_t)(syscall_kernel_stack + SYSCALL_KSTACK_SIZE);
    tss.iomap_base = sizeof(struct tss_entry);  /* pas de bitmap d'E/S -> ring3 n'a acces a AUCUN port */

    gdt_flush((uint32_t) &gdtp);
    tss_flush();
}

/* Permet de deplacer la pile "esp0" si necessaire (non utilise pour l'instant,
   notre demo ring3 est synchrone et garde toujours la meme pile de secours). */
void tss_set_kernel_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}
