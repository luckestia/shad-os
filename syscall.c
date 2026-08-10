/* syscall.c — Dispatcher des appels systeme, invoque depuis isr_handler()
 * (idt.c) pour l'interruption logicielle 0x80.
 *
 * Convention (inspiree de Linux) : numero d'appel dans EAX, arguments
 * dans EBX/ECX/EDX, valeur de retour renvoyee dans EAX.
 *
 * Comme la pagination est en identity-map (adresse virtuelle = adresse
 * physique) et que les segments sont "flat" (couvrent tout l'espace),
 * les pointeurs fournis par le code utilisateur pointent directement
 * vers la memoire reelle : pas de traduction d'adresse necessaire ici.
 * (Un vrai noyau multi-processus validerait ET traduirait ces pointeurs
 * via la table des pages du processus appelant.)
 */
#include "kernel.h"

#define SYS_EXIT  1
#define SYS_WRITE 4

extern void return_to_kernel(void);  /* low_level.S : "annule" la pile d'interruption en cours */

static int syscall_count = 0;

void syscall_dispatch(struct registers* regs) {
    syscall_count++;

    switch (regs->eax) {
        case SYS_WRITE: {
            /* Premier appel : preuve visible du changement de ring.
               regs->cs a ete pousse par le CPU AVANT d'entrer dans le
               noyau ; son RPL (bits 0-1) valait 3 au moment de l'appel
               puisque le code appelant tournait en ring3. */
            if (syscall_count == 1) {
                terminal_setcolor(vga_entry_color(VGA_YELLOW, VGA_BLACK));
                terminal_writestring("[noyau] syscall recu depuis CS=0x");
                terminal_write_hex(regs->cs);
                terminal_writestring(" (RPL=");
                terminal_write_uint(regs->cs & 0x3);
                terminal_writestring(") -> ring3 confirme\n");
                terminal_setcolor(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
            }

            const char* buf = (const char*) regs->ebx;
            uint32_t len = regs->ecx;
            for (uint32_t i = 0; i < len; i++) terminal_putchar(buf[i]);

            regs->eax = len;   /* valeur de retour : nombre d'octets ecrits */
            break;
        }

        case SYS_EXIT: {
            terminal_writestring("[noyau] syscall exit() recu, retour au contexte noyau.\n");
            return_to_kernel();   /* NE REVIENT JAMAIS ICI */
            break;                 /* jamais atteint */
        }

        default:
            terminal_writestring("[noyau] appel systeme inconnu.\n");
            regs->eax = (uint32_t) -1;
            break;
    }
}
