/* heap.c — Allocateur memoire dynamique (kmalloc / kfree).
 *
 * Principe (allocateur a liste chainee explicite, style K&R) :
 * le tas est une zone memoire continue, decoupee en blocs consecutifs.
 * Chaque bloc commence par un en-tete decrivant sa taille et son etat
 * (libre ou occupe), suivi des donnees utilisables.
 *
 *   [header][donnees....][header][donnees..][header][donnees.......]
 *
 * kmalloc() parcourt la liste (first-fit) et decoupe un bloc libre assez
 * grand si le reste vaut la peine d'etre garde comme bloc separe.
 * kfree() marque le bloc comme libre puis fusionne avec ses voisins
 * libres pour limiter la fragmentation.
 *
 * Cette zone memoire doit etre "identity-mappee" par la pagination
 * (paging.c) puisqu'on ne fait pas de demand-paging ici.
 */
#include "kernel.h"
#include "string.h"

/* Symbole fourni par le linker (linker.ld) : adresse de fin du kernel. */
extern uint8_t kernel_end;

#define HEAP_SIZE      (8 * 1024 * 1024)   /* 8 Mo de tas, dans les 16 Mo mappes */
#define ALIGNMENT      8
#define MIN_BLOCK_DATA 16   /* en dessous, ca ne vaut pas la peine de "split" */

typedef struct block_header {
    size_t size;              /* taille utilisable (hors en-tete), en octets */
    int free;                 /* 1 = libre, 0 = occupe */
    struct block_header* next;
} block_header_t;

static block_header_t* heap_start = NULL;
static uint32_t heap_base_addr = 0;
static uint32_t heap_total_size = 0;

static inline size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

void heap_init(void) {
    heap_base_addr = align_up((uint32_t) &kernel_end, 4096);
    heap_total_size = HEAP_SIZE;

    heap_start = (block_header_t*) heap_base_addr;
    heap_start->size = heap_total_size - sizeof(block_header_t);
    heap_start->free = 1;
    heap_start->next = NULL;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = align_up(size, ALIGNMENT);

    block_header_t* cur = heap_start;
    while (cur) {
        if (cur->free && cur->size >= size) {
            /* Si le reste du bloc est assez grand pour etre utile plus
               tard, on le decoupe en deux : le bloc alloue exactement a
               la bonne taille, et un nouveau bloc libre pour le reste. */
            size_t remaining = cur->size - size;
            if (remaining >= sizeof(block_header_t) + MIN_BLOCK_DATA) {
                block_header_t* new_block =
                    (block_header_t*)((uint8_t*) cur + sizeof(block_header_t) + size);
                new_block->size = remaining - sizeof(block_header_t);
                new_block->free = 1;
                new_block->next = cur->next;

                cur->size = size;
                cur->next = new_block;
            }
            cur->free = 0;
            return (uint8_t*) cur + sizeof(block_header_t);
        }
        cur = cur->next;
    }
    return NULL; /* tas sature */
}

/* Fusionne les blocs libres consecutifs pour limiter la fragmentation.
   Fonctionne car la liste reste triee par adresse croissante (kmalloc ne
   fait qu'inserer des blocs juste apres celui qu'il decoupe). */
static void coalesce(void) {
    block_header_t* cur = heap_start;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(block_header_t) + cur->next->size;
            cur->next = cur->next->next;
            /* ne pas avancer : re-tester avec le nouveau cur->next */
        } else {
            cur = cur->next;
        }
    }
}

void kfree(void* ptr) {
    if (!ptr) return;
    block_header_t* block = (block_header_t*)((uint8_t*) ptr - sizeof(block_header_t));
    block->free = 1;
    coalesce();
}

/* Statistiques pour la commande "meminfo" du shell */
void heap_get_stats(size_t* total, size_t* used, size_t* free_bytes, int* free_blocks) {
    size_t t = 0, u = 0, f = 0;
    int fb = 0;
    for (block_header_t* cur = heap_start; cur; cur = cur->next) {
        t += cur->size;
        if (cur->free) { f += cur->size; fb++; }
        else            { u += cur->size; }
    }
    if (total) *total = t;
    if (used) *used = u;
    if (free_bytes) *free_bytes = f;
    if (free_blocks) *free_blocks = fb;
}

uint32_t heap_get_base(void) {
    return heap_base_addr;
}
