/* tasking.c — Multitache preemptif cooperatif (structures de processus,
 * ordonnanceur round-robin, changement de contexte).
 *
 * Chaque tache possede sa propre pile (allouee via kmalloc, cf. heap.c),
 * dans laquelle son contexte CPU (registres callee-saved + point de
 * reprise) est sauvegarde a chaque changement de tache. L'ordonnanceur
 * est declenche periodiquement par l'interruption du timer (IRQ0), ce
 * qui rend le multitache preemptif : une tache n'a pas besoin de rendre
 * volontairement la main pour que les autres s'executent.
 */
#include "kernel.h"

#define TASK_STACK_SIZE 8192
#define SCHEDULE_EVERY_N_TICKS 5   /* quantum ~= 50ms a 100Hz */

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_TERMINATED,
} task_state_t;

typedef struct task {
    uint32_t esp;            /* pile sauvegardee (maj par switch_to) */
    uint8_t* stack_base;     /* pour liberer la pile a la terminaison */
    task_entry_t entry;
    int pid;
    task_state_t state;
    struct task* next;       /* liste circulaire (round-robin) */
} task_t;

/* Fonction assembleur definie dans low_level.S */
extern void switch_to(uint32_t* old_esp_ptr, uint32_t new_esp);

static task_t* current_task = NULL;
static task_t* zombie = NULL;      /* tache terminee en attente de liberation */
static int next_pid = 0;
static int tasking_ready = 0;
static uint32_t tick_counter = 0;
static volatile int tasking_paused = 0;

void tasking_set_paused(int paused) {
    tasking_paused = paused;
}

static void task_trampoline(void);

void tasking_init(void) {
    /* La tache 0 represente le flot d'execution ACTUEL (kernel_main lui-meme
       continuant vers la boucle du shell). On n'a pas besoin de fabriquer
       son contexte : son esp sera naturellement sauvegarde par switch_to()
       la premiere fois qu'on basculera vers une autre tache. */
    task_t* t0 = (task_t*) kmalloc(sizeof(task_t));
    t0->esp = 0;                 /* sera renseigne au premier changement de contexte */
    t0->stack_base = NULL;       /* pile du kernel lui-meme, ne pas liberer */
    t0->entry = NULL;
    t0->pid = next_pid++;
    t0->state = TASK_RUNNING;
    t0->next = t0;               /* liste circulaire d'un seul element */

    current_task = t0;
    tasking_ready = 1;
}

int task_create(task_entry_t entry) {
    task_t* t = (task_t*) kmalloc(sizeof(task_t));
    if (!t) return -1;

    uint8_t* stack = (uint8_t*) kmalloc(TASK_STACK_SIZE);
    if (!stack) { kfree(t); return -1; }

    /* Fabrique un contexte initial ayant EXACTEMENT la forme que switch_to()
       attend de trouver sur la pile d'une tache deja "suspendue" : les 4
       registres callee-saved (mis a 0, ils seront ecrases par task_trampoline
       de toute facon) suivis d'une adresse de retour factice qui pointe vers
       task_trampoline(). Le "ret" final de switch_to() sautera donc la-bas
       comme s'il revenait d'un appel normal. */
    uint32_t* sp = (uint32_t*) (stack + TASK_STACK_SIZE);
    sp -= 5;
    sp[0] = 0;                                /* edi */
    sp[1] = 0;                                /* esi */
    sp[2] = 0;                                /* ebx */
    sp[3] = 0;                                /* ebp */
    sp[4] = (uint32_t) task_trampoline;       /* adresse de "retour" */

    t->esp = (uint32_t) sp;
    t->stack_base = stack;
    t->entry = entry;
    t->pid = next_pid++;
    t->state = TASK_READY;

    /* Insertion dans la liste circulaire, juste apres la tache courante */
    t->next = current_task->next;
    current_task->next = t;

    return t->pid;
}

/* Point d'entree de toute nouvelle tache (jamais appelee directement en C :
   on y "atterrit" via le ret fabrique dans task_create()). */
static void task_trampoline(void) {
    __asm__ volatile ("sti");   /* le cli d'origine du stub IRQ ne nous concerne plus */

    if (current_task && current_task->entry) {
        current_task->entry();
    }

    task_exit();
    for (;;) { __asm__ volatile ("hlt"); }  /* filet de securite */
}

void task_exit(void) {
    __asm__ volatile ("cli");
    current_task->state = TASK_TERMINATED;
    schedule();
    /* schedule() ne revient jamais ici : on a bascule vers une autre tache */
}

void schedule(void) {
    if (!tasking_ready) return;

    /* Recupere la memoire de la tache terminee lors du switch precedent :
       on ne pouvait pas le faire plus tot puisqu'on tournait encore sur
       SA pile a ce moment-la. */
    if (zombie) {
        if (zombie->stack_base) kfree(zombie->stack_base);
        kfree(zombie);
        zombie = NULL;
    }

    task_t* prev = current_task;

    task_t* next = prev->next;
    while (next->state == TASK_TERMINATED && next != prev) {
        next = next->next;
    }
    if (next == prev && prev->state == TASK_TERMINATED) {
        return; /* plus aucune tache vivante : ne devrait jamais arriver (tache 0 = idle) */
    }
    if (next == prev) {
        return; /* une seule tache active : rien a faire */
    }

    if (prev->state == TASK_TERMINATED) {
        zombie = prev;
    } else if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }

    next->state = TASK_RUNNING;
    current_task = next;

    switch_to(&prev->esp, next->esp);
    /* Quand cette tache (prev) sera reprise plus tard, l'execution
       reviendra ici, juste apres cet appel. */
}

/* Appelee par timer_callback() a CHAQUE tick (100 fois/seconde) */
void tasking_tick(void) {
    if (!tasking_ready || tasking_paused) return;
    tick_counter++;
    if (tick_counter >= SCHEDULE_EVERY_N_TICKS) {
        tick_counter = 0;
        schedule();
    }
}

static const char* state_name(task_state_t s) {
    switch (s) {
        case TASK_READY:      return "prete";
        case TASK_RUNNING:    return "en cours";
        case TASK_TERMINATED: return "terminee";
        default:               return "?";
    }
}

void tasking_list(void) {
    if (!tasking_ready) {
        terminal_writestring("Multitache non initialise.\n");
        return;
    }
    terminal_writestring("PID  ETAT\n");
    task_t* t = current_task;
    do {
        terminal_writestring("  ");
        terminal_write_uint((uint32_t) t->pid);
        terminal_writestring("   ");
        terminal_writestring(state_name(t->state));
        terminal_writestring("\n");
        t = t->next;
    } while (t != current_task);
}
