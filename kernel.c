/* kernel.c — Point d'entree principal : initialise tous les sous-systemes
 * (terminal, GDT, IDT, PIC, clavier, pagination, tas, multitache) puis
 * rend la main aux interruptions.
 */
#include "kernel.h"
#include "string.h"

/* --- Deux taches de demonstration --- *
 * Elles tournent en arriere-plan, preemptees periodiquement par le timer,
 * et affichent chacune un petit "spinner" anime a une position fixe (ligne
 * de statut reservee) pour prouver visuellement qu'elles s'executent en
 * alternance avec le shell, sans jamais se bloquer mutuellement. */

static void spin_delay(volatile uint32_t count) {
    while (count--) { __asm__ volatile ("nop"); }
}

static void task_demo_a(void) {
    const char frames[] = { '|', '/', '-', '\\' };
    int i = 0;
    for (;;) {
        terminal_put_at(24, 0, 'A', vga_entry_color(VGA_CYAN, VGA_BLACK));
        terminal_put_at(24, 2, frames[i++ % 4], vga_entry_color(VGA_CYAN, VGA_BLACK));
        spin_delay(3000000);
    }
}

static void task_demo_b(void) {
    const char frames[] = { '\\', '-', '/', '|' };
    int i = 0;
    for (;;) {
        terminal_put_at(24, 5, 'B', vga_entry_color(VGA_LIGHT_RED, VGA_BLACK));
        terminal_put_at(24, 7, frames[i++ % 4], vga_entry_color(VGA_LIGHT_RED, VGA_BLACK));
        spin_delay(4500000);
    }
}

/* Logo ASCII de demarrage — tribut stylise au logo ShadestiaOS (cercle
   bicolore noir/or). Le mode texte VGA ne permet evidemment pas de
   reproduire le degrade reel du logo, mais un cercle divise en diagonale
   avec les memes couleurs (gris fonce / or) en donne un echo reconnaissable.
   Volontairement compact (7 lignes) pour rester visible en entier une fois
   le shell pret, malgre le defilement des ~13 lignes de messages de boot
   qui suivent sur un ecran de 25 lignes. */
#define LOGO_RX 8
#define LOGO_RY 3
#define LOGO_CY 4

static void draw_boot_logo(void) {
    const int cx = 40, cy = LOGO_CY;
    const int rx = LOGO_RX, ry = LOGO_RY;
    uint8_t gold = vga_entry_color(VGA_YELLOW, VGA_BLACK);
    uint8_t dark = vga_entry_color(VGA_DARK_GREY, VGA_BLACK);

    for (int dy = -ry; dy <= ry; dy++) {
        int ady = dy < 0 ? -dy : dy;
        int width = rx - (ady * ady * rx) / (ry * ry);
        for (int dx = -width; dx <= width; dx++) {
            uint8_t color = (dx - dy) >= 0 ? gold : dark;
            terminal_put_at((size_t)(cy + dy), (size_t)(cx + dx), (char) 219 /* bloc plein CP437 */, color);
        }
    }

    const char* word = "S H A D E S T I A   O S";
    size_t start_col = (size_t)(cx - (int) strlen(word) / 2);
    for (size_t i = 0; word[i] != '\0'; i++) {
        terminal_put_at((size_t)(cy + ry + 2), start_col + i, word[i], gold);
    }
}

void kernel_main(uint32_t magic, uint32_t mb_info_addr) {
    serial_init();
    serial_writestring("\n--- ShadestiaOS: kernel_main() atteint ---\n");
    serial_writestring("magic="); serial_write_hex(magic); serial_putc('\n');
    serial_writestring("mb_info_addr="); serial_write_hex(mb_info_addr); serial_putc('\n');
    if (magic == 0x2BADB002 && mb_info_addr) {
        uint32_t flags = *((uint32_t*) mb_info_addr);
        serial_writestring("mb flags="); serial_write_hex(flags); serial_putc('\n');
    }

    terminal_initialize();
    draw_boot_logo();
    /* Le logo est dessine directement cellule par cellule (terminal_put_at),
       independamment du curseur defilant : on avance ce dernier juste sous
       le logo (derniere ligne utilisee = LOGO_CY + LOGO_RY + 2, + 1 ligne
       vide) pour reprendre l'affichage normal en dessous. */
    for (int i = 0; i < (LOGO_CY + LOGO_RY + 4); i++) terminal_putchar('\n');

    terminal_setcolor(vga_entry_color(VGA_GREEN, VGA_BLACK));
    terminal_writestring("Hello, World depuis ShadestiaOS !\n");

    terminal_setcolor(vga_entry_color(VGA_WHITE, VGA_BLACK));
    terminal_writestring("Initialisation du systeme...\n");

    gdt_init();
    terminal_writestring("  [OK] GDT chargee (+ TSS, segments ring3)\n");

    idt_init();
    terminal_writestring("  [OK] IDT chargee, PIC remappe (+ int 0x80 syscalls)\n");

    timer_init(100);   /* 100 interruptions/seconde (10 ms de resolution) */
    terminal_writestring("  [OK] Timer PIT initialise (100 Hz)\n");

    keyboard_init();
    terminal_writestring("  [OK] Driver clavier installe\n");

    mouse_init();
    terminal_writestring("  [OK] Driver souris PS/2 installe (IRQ12)\n");

    paging_init();
    terminal_writestring("  [OK] Pagination memoire activee (identity map 16 Mo)\n");

    heap_init();
    terminal_writestring("  [OK] Tas dynamique initialise (kmalloc/kfree, 8 Mo)\n");

    ramfs_init();
    terminal_writestring("  [OK] Systeme de fichiers RAM initialise\n");

    if (magic == 0x2BADB002) {
        graphics_init(mb_info_addr);
        if (graphics_available()) {
            terminal_writestring("  [OK] Framebuffer graphique detecte (commande 'gfx' pour demo)\n");
        } else {
            terminal_writestring("  [--] Pas de framebuffer graphique fourni par le chargeur\n");
        }
    } else {
        terminal_writestring("  [--] Chargeur non-Multiboot : pas d'infos graphiques disponibles\n");
    }

    net_init();

    tasking_init();
    terminal_writestring("  [OK] Multitache initialise (tache 0 = ce flot d'execution)\n");

    task_create(task_demo_a);
    task_create(task_demo_b);
    terminal_writestring("  [OK] 2 taches de demonstration creees (voir ligne du bas)\n");

    __asm__ volatile ("sti");   /* active les interruptions materielles */

    shell_init();

#ifdef SELFTEST
    /* Bloc de verification temporaire : exerce chaque sous-systeme
       directement (sans passer par le clavier) pour capture d'ecran de
       preuve. Retire de la version finale (compile uniquement si
       SELFTEST est defini). */
    terminal_writestring("\n--- AUTO-TEST (sans clavier) ---\n");

    terminal_writestring("[fs] ecriture fichier test.txt...\n");
    ramfs_write("test.txt", (const uint8_t*) "Bonjour ramfs!", 14);
    terminal_writestring("[fs] contenu de test.txt : ");
    uint8_t* fdata; size_t fsize;
    ramfs_read("test.txt", &fdata, &fsize);
    for (size_t i = 0; i < fsize; i++) terminal_putchar((char) fdata[i]);
    terminal_writestring("\n[fs] liste des fichiers :\n");
    ramfs_list();

    terminal_writestring("[net] etat : ");
    terminal_writestring(net_is_ready() ? "carte RTL8139 prete\n" : "aucune carte detectee\n");
    if (net_is_ready()) {
        uint8_t mac[6];
        rtl8139_get_mac(mac);
        terminal_writestring("[net] MAC = ");
        for (int i = 0; i < 6; i++) { terminal_write_hex(mac[i]); if (i<5) terminal_writestring(":"); }
        terminal_writestring("\n[net] envoi requete ARP...\n");
        uint8_t target_ip[4] = {10,0,2,2};
        net_send_arp_request(target_ip);
    }

    terminal_writestring("[ring3] lancement de la demo mode utilisateur...\n");
    usermode_run_demo();

    if (graphics_available()) {
        serial_writestring("[gfx] framebuffer disponible, appel graphics_demo()...\n");
        serial_writestring("[gfx] fb_width="); serial_write_hex(graphics_get_width());
        serial_writestring(" fb_height="); serial_write_hex(graphics_get_height());
        serial_putc('\n');
        terminal_writestring("[gfx] framebuffer disponible, lancement de la demo...\n");
        graphics_demo();
        serial_writestring("[gfx] graphics_demo() termine\n");
    } else {
        serial_writestring("[gfx] framebuffer NON disponible\n");
        terminal_writestring("[gfx] pas de framebuffer (normal pour shadestia.bin, voir shadestia-gfx.bin)\n");
    }

    terminal_writestring("--- FIN AUTO-TEST ---\n");
#endif

    /* Le kernel reste actif : clavier, timer et ordonnanceur sont traites
       de facon asynchrone via les interruptions. Cette boucle EST la tache 0
       (l'ordonnanceur peut la preempter comme n'importe quelle autre tache). */
    for (;;) {
        __asm__ volatile ("hlt");
    }
}
