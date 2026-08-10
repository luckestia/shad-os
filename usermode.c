/* usermode.c — Demonstration d'execution en ring 3 (mode utilisateur).
 *
 * user_task_entry() est du code C tout a fait ordinaire, mais on va
 * l'executer avec le registre CS charge sur le descripteur "code
 * utilisateur" (DPL=3) plutot que "code noyau" (DPL=0). A partir de la,
 * le CPU refuse toute instruction privilegiee (cli/sti, in/out, hlt,
 * ecriture de CR3, ...) : la seule facon pour ce code de demander quoi
 * que ce soit au noyau est l'instruction "int $0x80", qui est autorisee
 * car sa porte IDT a ete configuree avec DPL=3 (voir idt.c).
 */
#include "kernel.h"

#define SYS_EXIT  1
#define SYS_WRITE 4

extern void enter_usermode(void (*entry)(void), uint32_t user_stack_top);

/* Desactive la preemption pendant la demo : notre pile noyau de secours
   (syscall_kernel_stack, voir gdt.c) est unique et n'est pas integree a
   l'ordonnanceur round-robin de tasking.c. Si le planificateur venait a
   changer de tache PENDANT que nous sommes en ring3, il basculerait ESP
   vers la pile d'une autre tache et on ne reviendrait jamais proprement.
   Geler temporairement l'ordonnanceur (tasking_set_paused) evite ce cas
   sans toucher au fonctionnement normal du multitache. */

#define USER_STACK_SIZE 4096
static uint8_t user_stack[USER_STACK_SIZE] __attribute__((aligned(16)));

static inline int32_t syscall2(int num, uint32_t arg1, uint32_t arg2) {
    int32_t ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2));
    return ret;
}

static inline void syscall0(int num) {
    __asm__ volatile ("int $0x80" :: "a"(num) : "ebx", "ecx");
}

static const char msg[] =
    "Bonjour depuis le ring 3 (mode utilisateur, CPL=3) !\n"
    "Ce texte a ete transmis au noyau via un vrai appel systeme (int 0x80).\n";

/* Point d'entree execute en ring3. Ne PAS appeler directement de fonctions
   noyau ici (terminal_writestring, etc.) : ce sont des adresses valides en
   memoire (segmentation plate), mais toute instruction privilegiee qu'elles
   contiendraient declencherait une exception. Seul le chemin syscall est
   legitime. */
void user_task_entry(void) {
    syscall2(SYS_WRITE, (uint32_t) msg, sizeof(msg) - 1);
    syscall0(SYS_EXIT);

    /* Jamais atteint : exit() ne revient pas. Filet de securite au cas ou. */
    for (;;) { }
}

void usermode_run_demo(void) {
    terminal_writestring("Passage en ring3 (CPL=3)...\n");

    tasking_set_paused(1);
    enter_usermode(user_task_entry, (uint32_t)(user_stack + USER_STACK_SIZE));
    tasking_set_paused(0);

    terminal_writestring("De retour en ring0 (noyau), execution normale reprise.\n");
}
