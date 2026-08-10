/* terminal.c — Affichage en mode texte VGA (80x25), avec defilement */

#include "kernel.h"

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH   80
#define VGA_HEIGHT  25
#define TEXT_HEIGHT (VGA_HEIGHT - 1)  /* derniere ligne reservee au statut des taches */

static uint16_t* const vga_buffer = (uint16_t*) VGA_ADDRESS;
static size_t term_row = 0;
static size_t term_col = 0;
static uint8_t term_color;

uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char c, uint8_t color) {
    return (uint16_t) c | (uint16_t) color << 8;
}

void terminal_setcolor(uint8_t color) {
    term_color = color;
}

void terminal_initialize(void) {
    term_color = vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK);
    for (size_t y = 0; y < VGA_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', term_color);
    term_row = 0;
    term_col = 0;
}

/* Ecrit un caractere a une position fixe SANS toucher au curseur du shell
   (term_row/term_col). Utilise pour les indicateurs de taches en arriere-
   plan : plusieurs taches peuvent y ecrire sans se marcher dessus ni
   perturber l'affichage principal. */
void terminal_put_at(size_t row, size_t col, char c, uint8_t color) {
    if (row >= VGA_HEIGHT || col >= VGA_WIDTH) return;
    vga_buffer[row * VGA_WIDTH + col] = vga_entry((unsigned char) c, color);
}

/* Fait remonter la zone de texte (lignes 0 a TEXT_HEIGHT-1) d'une ligne ;
   la derniere ligne de l'ecran reste reservee et n'est jamais decalee. */
static void terminal_scroll(void) {
    for (size_t y = 1; y < TEXT_HEIGHT; y++)
        for (size_t x = 0; x < VGA_WIDTH; x++)
            vga_buffer[(y - 1) * VGA_WIDTH + x] = vga_buffer[y * VGA_WIDTH + x];

    for (size_t x = 0; x < VGA_WIDTH; x++)
        vga_buffer[(TEXT_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', term_color);

    term_row = TEXT_HEIGHT - 1;
}

void terminal_putchar(char c) {
    if (c == '\n') {
        term_col = 0;
        term_row++;
    } else if (c == '\r') {
        term_col = 0;
    } else {
        vga_buffer[term_row * VGA_WIDTH + term_col] = vga_entry((unsigned char) c, term_color);
        if (++term_col == VGA_WIDTH) {
            term_col = 0;
            term_row++;
        }
    }
    if (term_row >= TEXT_HEIGHT) terminal_scroll();
}

/* Efface le dernier caractere tape (utilise par le driver clavier) */
void terminal_backspace(void) {
    if (term_col == 0) {
        if (term_row == 0) return;
        term_row--;
        term_col = VGA_WIDTH - 1;
    } else {
        term_col--;
    }
    vga_buffer[term_row * VGA_WIDTH + term_col] = vga_entry(' ', term_color);
}

void terminal_writestring(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++)
        terminal_putchar(str[i]);
}
