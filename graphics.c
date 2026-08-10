/* graphics.c — Mode graphique via le framebuffer lineaire fourni par le
 * chargeur de boot (GRUB) a travers la structure multiboot_info, quand
 * l'en-tete Multiboot du kernel a demande un mode video (voir boot.S).
 *
 * Contrairement au mode texte VGA (buffer fixe a 0xB8000, 80x25
 * caracteres), ici on ecrit directement des pixels a une adresse memoire
 * physique arbitraire (fournie par GRUB/le BIOS via VBE), avec une
 * largeur de ligne ("pitch") qui n'est pas forcement egale a largeur*4
 * (alignement materiel), d'ou le calcul de position par ligne.
 *
 * Bascule irreversible sans reboot : une fois en mode graphique, le
 * mode texte legacy (0xB8000) n'est plus affiche par le materiel.
 */
#include "kernel.h"

/* Sous-ensemble de la structure multiboot_info (spec Multiboot 1) —
   seuls les champs jusqu'aux informations de framebuffer nous interessent. */
struct multiboot_info {
    uint32_t flags;
    uint32_t mem_lower, mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count, mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length, mmap_addr;
    uint32_t drives_length, drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info, vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg, vbe_interface_off, vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint8_t  color_info[6];
} __attribute__((packed));

#define MULTIBOOT_INFO_FRAMEBUFFER (1 << 12)

static uint32_t fb_addr  = 0;
static uint32_t fb_pitch = 0;
static uint32_t fb_width  = 0;
static uint32_t fb_height = 0;
static uint8_t  fb_bpp   = 0;
static int      fb_ok    = 0;

void graphics_init(uint32_t mb_info_addr) {
    if (mb_info_addr == 0) return;
    struct multiboot_info* mbi = (struct multiboot_info*) mb_info_addr;

    if (!(mbi->flags & MULTIBOOT_INFO_FRAMEBUFFER)) return;

    fb_addr   = (uint32_t) mbi->framebuffer_addr;  /* adresses <4Go en pratique */
    fb_pitch  = mbi->framebuffer_pitch;
    fb_width  = mbi->framebuffer_width;
    fb_height = mbi->framebuffer_height;
    fb_bpp    = mbi->framebuffer_bpp;

    if (fb_addr != 0 && fb_bpp == 32 && fb_width > 0 && fb_height > 0) {
        /* La pagination de base (paging.c) n'identity-mappe que les 16
           premiers Mo : le framebuffer vit presque toujours bien plus
           haut en memoire physique (fenetre MMIO du GPU). Sans ce
           mapping explicite, le premier putpixel() declenche un page
           fault silencieux (invisible en mode graphique). */
        paging_identity_map_region(fb_addr, fb_pitch * fb_height);
        fb_ok = 1;
    }
}

int graphics_available(void) { return fb_ok; }
uint32_t graphics_get_width(void)  { return fb_width; }
uint32_t graphics_get_height(void) { return fb_height; }

static inline void putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (x >= fb_width || y >= fb_height) return;
    uint8_t* row = (uint8_t*) fb_addr + y * fb_pitch;
    uint32_t* px = (uint32_t*)(row + x * 4);
    *px = color;
}

static void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            putpixel(x + i, y + j, color);
        }
    }
}

static void draw_hline(uint32_t x, uint32_t y, uint32_t w, uint32_t color) {
    for (uint32_t i = 0; i < w; i++) putpixel(x + i, y, color);
}

static void draw_vline(uint32_t x, uint32_t y, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h; j++) putpixel(x, y + j, color);
}

/* Logo vectoriel ShadestiaOS — version fidele (contrairement a l'ASCII
   art du mode texte) puisqu'on dispose ici de vrais pixels : cercle
   divise en diagonale (gris fonce / or) avec un accent blanc rappelant
   le halo du logo original. */
static void draw_logo(uint32_t center_x, uint32_t center_y, uint32_t radius) {
    int32_t r2 = (int32_t)(radius * radius);
    for (int32_t dy = -(int32_t) radius; dy <= (int32_t) radius; dy++) {
        for (int32_t dx = -(int32_t) radius; dx <= (int32_t) radius; dx++) {
            if (dx * dx + dy * dy <= r2) {
                uint32_t color = (dx - dy) >= 0 ? 0xF4C430u /* or */ : 0x1A1A2Eu /* noir bleute */;
                putpixel(center_x + (uint32_t)(int32_t)dx, center_y + (uint32_t)(int32_t)dy, color);
            }
        }
    }
    /* halo blanc, decale vers le haut-droite, comme sur le logo original */
    int32_t wr = (int32_t) radius / 3;
    int32_t wr2 = wr * wr;
    int32_t wcx = (int32_t) radius / 3;
    int32_t wcy = -(int32_t) radius / 3;
    for (int32_t dy = -wr; dy <= wr; dy++) {
        for (int32_t dx = -wr; dx <= wr; dx++) {
            if (dx * dx + dy * dy <= wr2) {
                putpixel(center_x + (uint32_t)(int32_t)(wcx + dx), center_y + (uint32_t)(int32_t)(wcy + dy), 0xFFFFFFu);
            }
        }
    }
}

/* Petit dessin de demonstration : fond degrade + rectangles de couleur +
   une grille — suffisant pour prouver visuellement (via capture d'ecran)
   que le framebuffer lineaire est reellement pilote pixel par pixel. */
void graphics_demo(void) {
    if (!fb_ok) {
        terminal_writestring("Mode graphique indisponible : le chargeur n'a fourni aucun\n");
        terminal_writestring("framebuffer lineaire (verifiez que GRUB a bien neqocie un mode VBE).\n");
        return;
    }

    /* Fond : degrade bleu nuit */
    for (uint32_t y = 0; y < fb_height; y++) {
        uint32_t shade = (y * 60) / (fb_height ? fb_height : 1);
        uint32_t color = (shade << 16) | (shade << 8) | (0x40 + shade);
        draw_hline(0, y, fb_width, color);
    }

    /* Trois rectangles de couleurs primaires */
    fill_rect(40, 40, 200, 120, 0xE74C3C);   /* rouge */
    fill_rect(280, 40, 200, 120, 0x2ECC71);  /* vert */
    fill_rect(520, 40, 200, 120, 0x3498DB);  /* bleu */

    /* Logo ShadestiaOS, dans l'espace libre a droite des rectangles */
    if (fb_width > 900) {
        draw_logo(fb_width - 200, 100, 80);
    }

    /* Degrade horizontal (niveaux de gris) */
    uint32_t band_w = fb_width < 1024 ? fb_width : 1024;
    for (uint32_t x = 0; x < band_w; x++) {
        uint32_t c = (x * 255) / (band_w ? band_w : 1);
        draw_vline(x, 200, 80, (c << 16) | (c << 8) | c);
    }

    /* Grille pour verifier l'alignement pixel-perfect */
    for (uint32_t x = 0; x < fb_width; x += 40) draw_vline(x, 300, fb_height > 300 ? fb_height - 300 : 0, 0x555555);
    for (uint32_t y = 300; y < fb_height; y += 40) draw_hline(0, y, fb_width, 0x555555);
}
