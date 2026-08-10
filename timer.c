/* timer.c — PIT (Programmable Interval Timer), genere IRQ0 a frequence fixe.
 * Sert d'horloge systeme (necessaire pour un futur ordonnanceur, et pour
 * la commande "uptime" du shell).
 */
#include "kernel.h"
#include "io.h"

static volatile uint32_t tick_count = 0;
static uint32_t ticks_per_second = 100;

static void timer_callback(struct registers* regs) {
    (void) regs;
    tick_count++;
    tasking_tick();   /* declenche l'ordonnanceur tous les N ticks (voir tasking.c) */

    /* Console serie : pas d'IRQ dediee installee (COM1/IRQ4), simple
       scrutation a chaque tick (10 ms) - largement assez reactif pour
       une console texte, et evite de configurer une interruption de
       plus pour un canal de test/administration secondaire. */
    while (serial_has_data()) {
        shell_handle_char(serial_read_char());
    }
}

void timer_init(uint32_t frequency_hz) {
    ticks_per_second = frequency_hz;
    irq_install_handler(0, timer_callback);

    /* L'oscillateur du PIT tourne a 1193182 Hz ; on programme un diviseur
       pour obtenir la frequence d'interruption souhaitee. */
    uint32_t divisor = 1193182 / frequency_hz;

    outb(0x43, 0x36);                         /* canal 0, mode 3 (onde carree), binaire */
    outb(0x40, divisor & 0xFF);               /* octet bas du diviseur */
    outb(0x40, (divisor >> 8) & 0xFF);        /* octet haut du diviseur */
}

uint32_t timer_get_ticks(void) {
    return tick_count;
}

uint32_t timer_get_seconds(void) {
    return tick_count / ticks_per_second;
}
