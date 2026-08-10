/* mouse.c — Driver souris PS/2, pilote par interruption (IRQ12).
 *
 * La souris PS/2 partage le controleur clavier (port 0x60/0x64). Il faut
 * d'abord l'activer explicitement ("peripherique auxiliaire"), puis lui
 * envoyer deux commandes (valeurs par defaut + activation du flux de
 * donnees). Une fois active, elle envoie des paquets de 3 octets a chaque
 * mouvement/clic : [flags][delta X][delta Y].
 *
 * L'etat courant (position, boutons) est affiche en continu sur la ligne
 * de statut (rangee 24), a droite de l'indicateur des taches.
 */
#include "kernel.h"
#include "io.h"
#include "pic.h"

#define MOUSE_STATUS_ROW 24
#define MOUSE_STATUS_COL 20

static uint8_t  mouse_cycle = 0;
static int8_t   mouse_packet[3];
static int      mouse_x = 40;
static int      mouse_y = 12;
static uint8_t  mouse_buttons = 0;
static int      mouse_ready = 0;

static void mouse_wait_input(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if ((inb(0x64) & 2) == 0) return;   /* buffer d'entree du controleur libre */
    }
}

static void mouse_wait_output(void) {
    uint32_t timeout = 100000;
    while (timeout--) {
        if ((inb(0x64) & 1) == 1) return;   /* donnee disponible en sortie */
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait_input();
    outb(0x64, 0xD4);      /* "le prochain octet est pour la souris" */
    mouse_wait_input();
    outb(0x60, data);
}

static uint8_t mouse_read(void) {
    mouse_wait_output();
    return inb(0x60);
}

/* Ecrit un nombre 0-999 sur 3 caracteres a position fixe (sans passer
   par le terminal defilant : on ne veut pas perturber le shell). */
static void put_num3(size_t row, size_t col, int n, uint8_t color) {
    if (n < 0) n = 0;
    if (n > 999) n = 999;
    terminal_put_at(row, col,     (char) ('0' + (n / 100) % 10), color);
    terminal_put_at(row, col + 1, (char) ('0' + (n / 10) % 10), color);
    terminal_put_at(row, col + 2, (char) ('0' + n % 10), color);
}

static void mouse_draw_status(void) {
    uint8_t base = vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK);
    const char* label = "Mouse x=000 y=000 L M R";
    for (size_t i = 0; label[i] != '\0'; i++) {
        terminal_put_at(MOUSE_STATUS_ROW, MOUSE_STATUS_COL + i, label[i], base);
    }
    put_num3(MOUSE_STATUS_ROW, MOUSE_STATUS_COL + 8, mouse_x, base);
    put_num3(MOUSE_STATUS_ROW, MOUSE_STATUS_COL + 14, mouse_y, base);

    uint8_t on  = vga_entry_color(VGA_BLACK, VGA_GREEN);
    uint8_t off = base;
    terminal_put_at(MOUSE_STATUS_ROW, MOUSE_STATUS_COL + 18, 'L', (mouse_buttons & 0x01) ? on : off);
    terminal_put_at(MOUSE_STATUS_ROW, MOUSE_STATUS_COL + 20, 'M', (mouse_buttons & 0x04) ? on : off);
    terminal_put_at(MOUSE_STATUS_ROW, MOUSE_STATUS_COL + 22, 'R', (mouse_buttons & 0x02) ? on : off);
}

static void mouse_callback(struct registers* regs) {
    (void) regs;
    uint8_t data = inb(0x60);

    switch (mouse_cycle) {
        case 0:
            /* Le bit 3 du premier octet est toujours a 1 : sert a se
               resynchroniser si jamais on a perdu le cadencement. */
            if (!(data & 0x08)) return;
            mouse_packet[0] = (int8_t) data;
            mouse_cycle = 1;
            break;
        case 1:
            mouse_packet[1] = (int8_t) data;
            mouse_cycle = 2;
            break;
        case 2:
            mouse_packet[2] = (int8_t) data;
            mouse_cycle = 0;

            mouse_buttons = (uint8_t) mouse_packet[0] & 0x07;
            mouse_x += mouse_packet[1];
            mouse_y -= mouse_packet[2];  /* axe Y inverse par convention PS/2 */

            if (mouse_x < 0) mouse_x = 0;
            if (mouse_x > 300) mouse_x = 300;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_y > 300) mouse_y = 300;

            mouse_draw_status();
            break;
    }
}

/* Vide tout octet residuel deja present dans le buffer de sortie du
   controleur (ex: octet d'auto-test 0xAA envoye au reset). Sans ca, ce
   genre d'octet parasite peut decaler d'un cran la lecture des accuses
   de reception (ACK) envoyes par la souris pendant l'initialisation,
   les laissant trainer jusqu'a ce que l'IRQ clavier les recupere par
   erreur une fois les interruptions activees plus tard. */
static void mouse_flush_output_buffer(void) {
    uint32_t guard = 16;
    while (guard-- && (inb(0x64) & 1)) {
        inb(0x60);
    }
}

void mouse_init(void) {
    mouse_flush_output_buffer();

    outb(0x64, 0xA8);              /* active le peripherique auxiliaire (souris) */

    mouse_wait_input();
    outb(0x64, 0x20);              /* lit l'octet de configuration du controleur */
    uint8_t status = mouse_read();
    status |= 0x02;                /* autorise IRQ12 */
    status &= ~0x20;               /* active l'horloge souris (bit "disable" a 0) */
    mouse_wait_input();
    outb(0x64, 0x60);
    mouse_wait_input();
    outb(0x60, status);

    mouse_write(0xF6);             /* "set defaults" */
    mouse_read();                  /* accuse de reception (0xFA) */
    mouse_write(0xF4);             /* "enable data reporting" */
    mouse_read();

    irq_install_handler(12, mouse_callback);
    pic_unmask_irq(12);
    pic_unmask_irq(2);             /* IRQ2 = cascade vers le PIC esclave, necessaire pour IRQ8-15 */

    mouse_ready = 1;
    mouse_draw_status();
}

void mouse_get_state(int* x, int* y, uint8_t* buttons) {
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (buttons) *buttons = mouse_buttons;
}

int mouse_is_ready(void) {
    return mouse_ready;
}
