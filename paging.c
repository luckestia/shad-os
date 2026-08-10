/* paging.c — Pagination memoire (x86 32 bits, pages de 4Ko).
 *
 * Principe : la pagination traduit des adresses "virtuelles" (celles vues
 * par le code) en adresses "physiques" (la RAM reelle), via deux niveaux
 * de tables : le Page Directory (1024 entrees, chacune couvre 4Mo) et les
 * Page Tables (1024 entrees, chacune couvre 4Ko).
 *
 * Ici on fait un "identity mapping" : adresse virtuelle == adresse
 * physique, sur les 16 premiers Mo (kernel + tas dynamique). C'est
 * l'etape indispensable avant d'implementer un vrai allocateur de
 * memoire virtuelle et la protection memoire entre processus.
 */
#include "kernel.h"
#include "io.h"

#define PAGE_PRESENT  0x1
#define PAGE_WRITE    0x2
#define PAGE_USER     0x4   /* sans ce bit, TOUTE page est reservee au ring0 :
                                le ring3 (voir usermode.c) prend un page fault
                                "violation de protection" au moindre acces
                                memoire, meme en lecture simple. Notre modele
                                est un identity-map "a plat" sans isolation
                                entre processus (comme les segments GDT), donc
                                on autorise le ring3 sur toute la zone mappee. */

#define NUM_PAGE_TABLES 4   /* 4 x 4 Mo = 16 Mo de memoire identity-mappee */

/* Alignees sur 4Ko : exigence stricte du CPU pour CR3 et les entrees. */
static uint32_t page_directory[1024] __attribute__((aligned(4096)));
static uint32_t page_tables[NUM_PAGE_TABLES][1024] __attribute__((aligned(4096)));

static inline uint32_t read_cr2(void) {
    uint32_t val;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(val));
    return val;
}

static inline void load_page_directory(uint32_t* dir) {
    __asm__ volatile ("mov %0, %%cr3" :: "r"(dir));
}

static inline void enable_paging(void) {
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;   /* bit PG (paging enable) */
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0));
}

/* Tables de pages de secours, pour mapper a la demande des regions
   physiques HORS des 16 Mo de base ci-dessus — typiquement le
   framebuffer VBE/VESA (graphics.c), qui vit presque toujours a une
   adresse physique elevee (ex: 0xFD000000, dans la fenetre MMIO du
   GPU), tres loin de nos 16 Mo identity-mappes. Sans ce mapping
   supplementaire, le premier acces au framebuffer declenche un page
   fault (page absente) — invisible en mode graphique puisque notre
   gestionnaire de page fault ecrit sur le buffer texte VGA legacy,
   lui-meme non affiche une fois le mode graphique actif : le systeme
   semble juste "bloque" sans aucun message a l'ecran. */
#define NUM_EXTRA_TABLES 4
static uint32_t extra_page_tables[NUM_EXTRA_TABLES][1024] __attribute__((aligned(4096)));
static int extra_tables_used = 0;

void paging_identity_map_region(uint32_t phys_addr, uint32_t size) {
    uint32_t start = phys_addr & ~0xFFFu;
    uint32_t end   = (phys_addr + size + 0xFFF) & ~0xFFFu;

    for (uint32_t addr = start; addr < end; addr += 0x1000) {
        uint32_t pd_index = addr >> 22;
        uint32_t pt_index = (addr >> 12) & 0x3FF;

        if (!(page_directory[pd_index] & PAGE_PRESENT)) {
            if (extra_tables_used >= NUM_EXTRA_TABLES) return;  /* plus de table de secours disponible */
            uint32_t* new_table = extra_page_tables[extra_tables_used++];
            for (int i = 0; i < 1024; i++) new_table[i] = 0;
            page_directory[pd_index] = ((uint32_t) new_table) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        }

        uint32_t* table = (uint32_t*)(page_directory[pd_index] & ~0xFFFu);
        table[pt_index] = addr | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
}

void paging_init(void) {
    /* Identity map des 16 premiers Mo : 4 page tables de 4 Mo chacune.
       entree i de la table t -> adresse physique t*4Mo + i*4Ko */
    for (int t = 0; t < NUM_PAGE_TABLES; t++) {
        for (int i = 0; i < 1024; i++) {
            uint32_t phys_addr = (t * 0x400000) + (i * 0x1000);
            page_tables[t][i] = phys_addr | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        }
    }

    for (int i = 0; i < 1024; i++) {
        page_directory[i] = 0; /* non presente par defaut */
    }
    for (int t = 0; t < NUM_PAGE_TABLES; t++) {
        page_directory[t] = ((uint32_t) page_tables[t]) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    load_page_directory(page_directory);
    enable_paging();
}

/* Gestionnaire du Page Fault (interruption 14), appele depuis idt.c.
 * Affiche l'adresse fautive (registre CR2) et le type de faute avant
 * d'arreter le systeme — un vrai OS tenterait ici de charger la page
 * manquante depuis le disque (swap) ou de tuer le processus fautif. */
void page_fault_handler(struct registers* regs) {
    uint32_t fault_addr = read_cr2();

    int present  = !(regs->err_code & 0x1);  /* page non presente ? */
    int rw       = regs->err_code & 0x2;      /* ecriture ? */
    int user     = regs->err_code & 0x4;      /* mode utilisateur ? */

    terminal_setcolor(vga_entry_color(VGA_WHITE, VGA_RED));
    terminal_writestring("\n*** PAGE FAULT *** adresse=");
    terminal_write_hex(fault_addr);
    terminal_writestring(present ? " (page absente)" : " (violation de protection)");
    terminal_writestring(rw ? ", ecriture" : ", lecture");
    terminal_writestring(user ? ", mode utilisateur" : ", mode noyau");
    terminal_writestring("\nSysteme arrete.\n");
    terminal_setcolor(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));

    for (;;) { __asm__ volatile ("cli; hlt"); }
}
