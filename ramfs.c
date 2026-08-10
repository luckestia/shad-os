/* ramfs.c — Systeme de fichiers minimal, entierement en RAM.
 *
 * Espace de noms plat (pas de sous-dossiers) : une simple table de fichiers,
 * chacun avec un contenu alloue dynamiquement via kmalloc. Le contenu est
 * remplace (jamais modifie en place) a chaque ecriture : on kmalloc un
 * nouveau buffer de la bonne taille, on recopie, et on libere l'ancien.
 * Suffisant pour demontrer un vrai systeme de fichiers fonctionnel
 * (creation, lecture, ecriture, suppression, listage) sans la complexite
 * d'un vrai format sur disque (inodes, blocs, etc.) puisque tout vit en
 * memoire et disparait au reboot.
 */
#include "kernel.h"
#include "string.h"

#define RAMFS_MAX_FILES     32
#define RAMFS_MAX_NAME      32

typedef struct {
    char     name[RAMFS_MAX_NAME];
    uint8_t* data;
    size_t   size;
    int      used;
} ramfs_file_t;

static ramfs_file_t files[RAMFS_MAX_FILES];
static int ramfs_ready = 0;

void ramfs_init(void) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        files[i].used = 0;
        files[i].data = NULL;
        files[i].size = 0;
    }
    ramfs_ready = 1;
}

static int ramfs_find(const char* name) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (files[i].used && strcmp(files[i].name, name) == 0) return i;
    }
    return -1;
}

static int ramfs_find_free(void) {
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!files[i].used) return i;
    }
    return -1;
}

/* Cree le fichier s'il n'existe pas encore, puis (re)ecrit son contenu.
   Renvoie 0 si succes, -1 si echec (table pleine ou tas sature). */
int ramfs_write(const char* name, const uint8_t* data, size_t size) {
    if (!ramfs_ready || strlen(name) >= RAMFS_MAX_NAME) return -1;

    int idx = ramfs_find(name);
    if (idx < 0) {
        idx = ramfs_find_free();
        if (idx < 0) return -1;  /* table de fichiers pleine */

        int i = 0;
        while (name[i] && i < RAMFS_MAX_NAME - 1) { files[idx].name[i] = name[i]; i++; }
        files[idx].name[i] = '\0';
        files[idx].used = 1;
    }

    uint8_t* newbuf = NULL;
    if (size > 0) {
        newbuf = (uint8_t*) kmalloc(size);
        if (!newbuf) return -1;  /* tas sature : on garde l'ancien contenu intact */
        memcpy_ramfs(newbuf, data, size);
    }

    if (files[idx].data) kfree(files[idx].data);
    files[idx].data = newbuf;
    files[idx].size = size;
    return 0;
}

/* Petit memcpy local (string.h n'en fournit pas) */
void memcpy_ramfs(uint8_t* dst, const uint8_t* src, size_t n) {
    for (size_t i = 0; i < n; i++) dst[i] = src[i];
}

/* Renvoie un pointeur vers le contenu (NE PAS liberer) et sa taille.
   Renvoie -1 si le fichier n'existe pas. */
int ramfs_read(const char* name, uint8_t** out_data, size_t* out_size) {
    int idx = ramfs_find(name);
    if (idx < 0) return -1;
    if (out_data) *out_data = files[idx].data;
    if (out_size) *out_size = files[idx].size;
    return 0;
}

int ramfs_delete(const char* name) {
    int idx = ramfs_find(name);
    if (idx < 0) return -1;
    if (files[idx].data) kfree(files[idx].data);
    files[idx].data = NULL;
    files[idx].size = 0;
    files[idx].used = 0;
    return 0;
}

/* Liste tous les fichiers via terminal_writestring (utilise par "ls") */
void ramfs_list(void) {
    int count = 0;
    for (int i = 0; i < RAMFS_MAX_FILES; i++) {
        if (!files[i].used) continue;
        count++;
        terminal_writestring("  ");
        terminal_writestring(files[i].name);
        terminal_writestring("  (");
        terminal_write_uint((uint32_t) files[i].size);
        terminal_writestring(" octets)\n");
    }
    if (count == 0) terminal_writestring("  (aucun fichier)\n");
}
