/* serial.c — Sortie de diagnostic via le port serie COM1 (0x3F8).
 * Utile pour deboguer des situations ou l'affichage VGA/graphique n'est
 * pas fiable (ex: mode video bascule par le chargeur de boot avant meme
 * que le noyau ne s'execute). Independant du terminal principal.
 */
#include "kernel.h"
#include "io.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);    /* desactive les interruptions */
    outb(COM1 + 3, 0x80);    /* active DLAB pour configurer le diviseur */
    outb(COM1 + 0, 0x03);    /* diviseur = 3 -> 38400 bauds */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);    /* 8 bits, pas de parite, 1 bit de stop */

    /* PIO pur (mode 16450 classique) plutot que FIFO : bit0 du FCR a 0
       desactive le tampon materiel 16 octets du 16550. Chaque caractere
       est alors transfere individuellement entre le CPU et l'UART, sans
       mise en file d'attente cote materiel - ce qui correspond de toute
       facon exactement a la maniere dont ce driver est utilise (un octet
       a la fois via serial_putc/serial_read_char, jamais de rafale). */
    outb(COM1 + 2, 0x00);    /* FIFO desactive : mode PIO */

    outb(COM1 + 4, 0x0B);    /* IRQ activees, RTS/DSR */
}

static int serial_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

int serial_has_data(void) {
    return inb(COM1 + 5) & 0x01;
}

char serial_read_char(void) {
    return (char) inb(COM1);
}

void serial_putc(char c) {
    while (!serial_transmit_empty()) { }
    outb(COM1, (uint8_t) c);
}

void serial_writestring(const char* s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

void serial_write_hex(uint32_t n) {
    const char* hex = "0123456789ABCDEF";
    serial_writestring("0x");
    for (int i = 28; i >= 0; i -= 4) serial_putc(hex[(n >> i) & 0xF]);
}
