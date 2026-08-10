/* shell.c — Interpreteur de commandes minimal.
 * Recoit les caracteres un par un depuis le driver clavier, accumule une
 * ligne, et l'execute quand l'utilisateur appuie sur Entree.
 */
#include "kernel.h"
#include "string.h"
#include "io.h"

#define CMD_BUFFER_SIZE 128
static char cmd_buffer[CMD_BUFFER_SIZE];
static size_t cmd_len = 0;

static void print_prompt(void) {
    terminal_setcolor(vga_entry_color(VGA_YELLOW, VGA_BLACK));
    terminal_writestring("\nshadestia> ");
    terminal_setcolor(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
}

/* Coupe la chaine au premier espace et renvoie un pointeur vers les
   arguments (ou vers la fin de chaine "\0" s'il n'y en a pas). */
static char* split_command(char* line) {
    char* p = line;
    while (*p && *p != ' ') p++;
    if (*p == ' ') {
        *p = '\0';
        p++;
        while (*p == ' ') p++;  /* saute les espaces multiples */
    }
    return p;
}

static void cmd_help(void) {
    terminal_writestring(
        "Commandes disponibles :\n"
        "  help     - affiche cette aide\n"
        "  clear    - efface l'ecran\n"
        "  echo T   - affiche le texte T\n"
        "  uptime   - temps ecoule depuis le demarrage\n"
        "  meminfo  - etat du tas dynamique (kmalloc/kfree)\n"
        "  alloctest- demontre kmalloc/kfree en direct\n"
        "  ps       - liste les taches et leur etat\n"
        "  ls       - liste les fichiers (systeme de fichiers RAM)\n"
        "  cat F    - affiche le contenu du fichier F\n"
        "  write F T- ecrit le texte T dans le fichier F (le cree si besoin)\n"
        "  rm F     - supprime le fichier F\n"
        "  mouse    - affiche l'etat courant de la souris PS/2\n"
        "  gfx      - demo mode graphique VESA (irreversible sans reboot)\n"
        "  usertest - execute du code en ring3 (mode utilisateur) via syscall\n"
        "  nettest  - detecte la carte reseau et envoie une requete ARP\n"
        "  reboot   - redemarre la machine\n"
    );
}

static void cmd_uptime(void) {
    terminal_writestring("Uptime : ");
    terminal_write_uint(timer_get_seconds());
    terminal_writestring(" s (");
    terminal_write_uint(timer_get_ticks());
    terminal_writestring(" ticks)\n");
}

static void cmd_meminfo(void) {
    size_t total, used, free_bytes;
    int free_blocks;
    heap_get_stats(&total, &used, &free_bytes, &free_blocks);

    terminal_writestring(
        "Pagination : ACTIVE (identity mapping 16 Mo)\n"
        "Tas dynamique (heap) :\n"
        "  Base       : ");
    terminal_write_hex(heap_get_base());
    terminal_writestring("\n  Taille totale : ");
    terminal_write_uint((uint32_t) total);
    terminal_writestring(" octets\n  Utilisee      : ");
    terminal_write_uint((uint32_t) used);
    terminal_writestring(" octets\n  Libre         : ");
    terminal_write_uint((uint32_t) free_bytes);
    terminal_writestring(" octets\n  Blocs libres  : ");
    terminal_write_uint((uint32_t) free_blocks);
    terminal_writestring("\n");
}

/* Demonstration concrete de kmalloc/kfree : alloue trois blocs de tailles
   differentes, y ecrit des donnees, les relit, puis libere le tout — en
   affichant les stats du tas avant/apres pour prouver que la memoire est
   bien recuperee (pas de fuite). */
static void cmd_alloctest(void) {
    size_t total, used, free_bytes;
    int free_blocks;

    terminal_writestring("Avant allocation :\n");
    heap_get_stats(&total, &used, &free_bytes, &free_blocks);
    terminal_writestring("  utilise=");
    terminal_write_uint((uint32_t) used);
    terminal_writestring(" octets, libre=");
    terminal_write_uint((uint32_t) free_bytes);
    terminal_writestring(" octets\n");

    char* a = (char*) kmalloc(32);
    char* b = (char*) kmalloc(128);
    int*  c = (int*)  kmalloc(sizeof(int) * 10);

    if (!a || !b || !c) {
        terminal_writestring("Echec d'allocation !\n");
        return;
    }

    strcmp(a, a); /* no-op, juste pour montrer que a est utilisable */
    for (int i = 0; i < 31; i++) a[i] = 'A' + (i % 26);
    a[31] = '\0';
    for (int i = 0; i < 10; i++) c[i] = i * i;

    terminal_writestring("Allocations : a(32o)=");
    terminal_write_hex((uint32_t) a);
    terminal_writestring(" b(128o)=");
    terminal_write_hex((uint32_t) b);
    terminal_writestring(" c(40o)=");
    terminal_write_hex((uint32_t) c);
    terminal_writestring("\n");

    terminal_writestring("Contenu de a  : ");
    terminal_writestring(a);
    terminal_writestring("\nContenu de c  : c[9]=");
    terminal_write_uint((uint32_t) c[9]);
    terminal_writestring(" (attendu 81)\n");

    terminal_writestring("Apres allocation :\n");
    heap_get_stats(&total, &used, &free_bytes, &free_blocks);
    terminal_writestring("  utilise=");
    terminal_write_uint((uint32_t) used);
    terminal_writestring(" octets, libre=");
    terminal_write_uint((uint32_t) free_bytes);
    terminal_writestring(" octets\n");

    kfree(a);
    kfree(b);
    kfree(c);

    terminal_writestring("Apres liberation (kfree) :\n");
    heap_get_stats(&total, &used, &free_bytes, &free_blocks);
    terminal_writestring("  utilise=");
    terminal_write_uint((uint32_t) used);
    terminal_writestring(" octets, libre=");
    terminal_write_uint((uint32_t) free_bytes);
    terminal_writestring(" octets, blocs libres=");
    terminal_write_uint((uint32_t) free_blocks);
    terminal_writestring("\n");
}

static void cmd_reboot(void) {
    terminal_writestring("Redemarrage...\n");
    /* Technique classique : impulsion sur la ligne de reset du controleur
       clavier 8042 (port 0x64, commande 0xFE). */
    uint8_t good = 0x02;
    while (good & 0x02) good = inb(0x64);
    outb(0x64, 0xFE);
    for (;;) { __asm__ volatile ("cli; hlt"); } /* si le reboot echoue */
}

/* --- Systeme de fichiers --- */

static void cmd_cat(const char* filename) {
    if (filename[0] == '\0') {
        terminal_writestring("Usage : cat <fichier>\n");
        return;
    }
    uint8_t* data;
    size_t size;
    if (ramfs_read(filename, &data, &size) != 0) {
        terminal_writestring("Fichier introuvable : ");
        terminal_writestring(filename);
        terminal_writestring("\n");
        return;
    }
    for (size_t i = 0; i < size; i++) terminal_putchar((char) data[i]);
    if (size == 0 || data[size - 1] != '\n') terminal_putchar('\n');
}

static void cmd_write(char* args) {
    char* content = split_command(args);   /* args = nom, content = reste de la ligne */
    if (args[0] == '\0') {
        terminal_writestring("Usage : write <fichier> <texte>\n");
        return;
    }
    if (ramfs_write(args, (const uint8_t*) content, strlen(content)) == 0) {
        terminal_writestring("Ecrit : ");
        terminal_writestring(args);
        terminal_writestring(" (");
        terminal_write_uint((uint32_t) strlen(content));
        terminal_writestring(" octets)\n");
    } else {
        terminal_writestring("Echec de l'ecriture (table de fichiers pleine ou tas sature).\n");
    }
}

static void cmd_rm(const char* filename) {
    if (filename[0] == '\0') {
        terminal_writestring("Usage : rm <fichier>\n");
        return;
    }
    if (ramfs_delete(filename) == 0) {
        terminal_writestring("Supprime : ");
        terminal_writestring(filename);
        terminal_writestring("\n");
    } else {
        terminal_writestring("Fichier introuvable : ");
        terminal_writestring(filename);
        terminal_writestring("\n");
    }
}

/* --- Souris --- */

static void cmd_mouse(void) {
    int x, y;
    uint8_t buttons;
    mouse_get_state(&x, &y, &buttons);
    terminal_writestring("Souris PS/2 : ");
    terminal_writestring(mouse_is_ready() ? "active\n" : "non initialisee\n");
    terminal_writestring("  Position : x=");
    terminal_write_uint((uint32_t) x);
    terminal_writestring(" y=");
    terminal_write_uint((uint32_t) y);
    terminal_writestring("\n  Boutons  : gauche=");
    terminal_write_uint(buttons & 0x01);
    terminal_writestring(" milieu=");
    terminal_write_uint((buttons >> 2) & 0x01);
    terminal_writestring(" droit=");
    terminal_write_uint((buttons >> 1) & 0x01);
    terminal_writestring("\n  (La position se met a jour en direct en bas a droite de l'ecran.)\n");
}

/* --- Mode graphique --- */

static void cmd_gfx(void) {
    if (!graphics_available()) {
        terminal_writestring("Framebuffer non disponible pour cette session de boot.\n");
        return;
    }
    terminal_writestring("Basculement en mode graphique (");
    terminal_write_uint(graphics_get_width());
    terminal_writestring("x");
    terminal_write_uint(graphics_get_height());
    terminal_writestring(", irreversible sans reboot)...\n");
    graphics_demo();
}

/* --- Reseau --- */

static void cmd_nettest(void) {
    if (!net_is_ready()) {
        terminal_writestring("Aucune carte reseau RTL8139 detectee sur le bus PCI.\n");
        return;
    }
    uint8_t mac[6];
    rtl8139_get_mac(mac);
    terminal_writestring("Carte RTL8139 initialisee. Adresse MAC : ");
    for (int i = 0; i < 6; i++) {
        terminal_write_hex(mac[i]);
        if (i < 5) terminal_writestring(":");
    }
    terminal_writestring("\n");

    uint8_t target_ip[4] = { 10, 0, 2, 2 };  /* passerelle par defaut du reseau QEMU "user" */
    terminal_writestring("Envoi d'une requete ARP broadcast (qui a 10.0.2.2 ?)...\n");
    if (net_send_arp_request(target_ip) == 0) {
        terminal_writestring("Trame envoyee a la carte reseau.\n");
    } else {
        terminal_writestring("Echec de l'envoi.\n");
    }
    terminal_writestring("Paquets recus depuis le demarrage : ");
    terminal_write_uint(rtl8139_get_rx_count());
    terminal_writestring("\n");
}

static void shell_execute(char* line) {
    char* args = split_command(line);

    if (line[0] == '\0') {
        /* ligne vide : ne rien faire */
    } else if (strcmp(line, "help") == 0) {
        cmd_help();
    } else if (strcmp(line, "clear") == 0) {
        terminal_initialize();
        return; /* pas de nouveau prompt precede d'un \n */
    } else if (strcmp(line, "echo") == 0) {
        terminal_writestring(args);
        terminal_writestring("\n");
    } else if (strcmp(line, "uptime") == 0) {
        cmd_uptime();
    } else if (strcmp(line, "meminfo") == 0) {
        cmd_meminfo();
    } else if (strcmp(line, "alloctest") == 0) {
        cmd_alloctest();
    } else if (strcmp(line, "ps") == 0) {
        tasking_list();
    } else if (strcmp(line, "ls") == 0) {
        ramfs_list();
    } else if (strcmp(line, "cat") == 0) {
        cmd_cat(args);
    } else if (strcmp(line, "write") == 0) {
        cmd_write(args);
    } else if (strcmp(line, "rm") == 0) {
        cmd_rm(args);
    } else if (strcmp(line, "mouse") == 0) {
        cmd_mouse();
    } else if (strcmp(line, "gfx") == 0) {
        cmd_gfx();
    } else if (strcmp(line, "usertest") == 0) {
        usermode_run_demo();
    } else if (strcmp(line, "nettest") == 0) {
        cmd_nettest();
    } else if (strcmp(line, "reboot") == 0) {
        cmd_reboot();
    } else {
        terminal_writestring("Commande inconnue : ");
        terminal_writestring(line);
        terminal_writestring(" (tapez 'help')\n");
    }
    print_prompt();
}

void shell_init(void) {
    cmd_len = 0;
    terminal_writestring("\nTapez 'help' pour la liste des commandes.");
    print_prompt();
}

/* Appelee par keyboard.c a chaque caractere saisi */
void shell_handle_char(char c) {
    if (c == '\n') {
        terminal_putchar('\n');
        cmd_buffer[cmd_len] = '\0';
        shell_execute(cmd_buffer);
        cmd_len = 0;
    } else if (c == '\b') {
        if (cmd_len > 0) {
            cmd_len--;
            terminal_backspace();
        }
    } else {
        if (cmd_len < CMD_BUFFER_SIZE - 1) {
            cmd_buffer[cmd_len++] = c;
            terminal_putchar(c);
        }
    }
}
