/* pic.c — Programmable Interrupt Controller (8259).
 *
 * Par defaut, le PIC envoie les IRQ materielles (clavier, timer, ...)
 * sur les vecteurs d'interruption 0-15, qui entrent en collision avec
 * les exceptions CPU (division par zero, etc.). On les "remappe" pour
 * qu'elles arrivent sur les vecteurs 32-47 a la place.
 */
#include "kernel.h"
#include "io.h"

#define PIC1        0x20   /* PIC maitre : port commande */
#define PIC1_DATA   0x21
#define PIC2        0xA0   /* PIC esclave : port commande */
#define PIC2_DATA   0xA1

#define PIC_EOI     0x20   /* commande "fin d'interruption" */
#define ICW1_INIT   0x10
#define ICW1_ICW4   0x01
#define ICW4_8086   0x01

void pic_remap(void) {
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    outb(PIC1, ICW1_INIT | ICW1_ICW4); io_wait();
    outb(PIC2, ICW1_INIT | ICW1_ICW4); io_wait();

    outb(PIC1_DATA, 32);   io_wait();  /* IRQ maitre -> vecteurs 32-39 */
    outb(PIC2_DATA, 40);   io_wait();  /* IRQ esclave -> vecteurs 40-47 */

    outb(PIC1_DATA, 4);    io_wait();  /* indique au maitre l'esclave sur IRQ2 */
    outb(PIC2_DATA, 2);    io_wait();  /* indique a l'esclave son numero de cascade */

    outb(PIC1_DATA, ICW4_8086); io_wait();
    outb(PIC2_DATA, ICW4_8086); io_wait();

    outb(PIC1_DATA, mask1);  /* restaure les masques d'origine */
    outb(PIC2_DATA, mask2);
}

/* Chaque IRQ traitee doit etre "acquittee" sinon le PIC bloque les suivantes */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) outb(PIC2, PIC_EOI);  /* IRQ esclave : acquitter les deux PIC */
    outb(PIC1, PIC_EOI);
}

/* Autorise (demasque) une IRQ specifique */
void pic_unmask_irq(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC1_DATA : PIC2_DATA;
    uint8_t irq_line = (irq < 8) ? irq : irq - 8;
    uint8_t mask = inb(port);
    outb(port, mask & ~(1 << irq_line));
}
