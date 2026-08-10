/* keyboard.c — Driver clavier PS/2 basique (disposition US QWERTY).
 *
 * Le clavier genere un "scancode" a chaque appui (make code) et relachement
 * (break code = make code + 0x80) de touche, lu sur le port 0x60.
 */
#include "kernel.h"
#include "io.h"
#include "pic.h"

/* Table de correspondance scancode -> caractere ASCII (disposition US, non-shift) */
static const char scancode_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b', /* 0x00-0x0E : Esc, chiffres, Backspace */
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',    /* 0x0F-0x1C : Tab, ligne QWERTY, Enter */
    0, 'a','s','d','f','g','h','j','k','l',';','\'','`',           /* 0x1D-0x29 : Ctrl (0), ligne ASDF */
    0, '\\','z','x','c','v','b','n','m',',','.','/', 0,             /* 0x2A-0x36 : Shift gauche (0), ligne ZXCV, Shift droit (0) */
    '*', 0, ' ',                                                    /* 0x37-0x39 : pave num *, Alt (0), Espace */
    /* le reste (touches F1-F12, pave numerique, etc.) n'est pas gere ici */
};

/* Appelee automatiquement par irq_handler() quand IRQ1 se declenche */
static void keyboard_callback(struct registers* regs) {
    (void) regs;
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) {
        /* Touche relachee (break code) : on ignore pour ce driver minimal */
        return;
    }

    if (scancode < 128) {
        char c = scancode_ascii[scancode];
        if (c != 0) {
            shell_handle_char(c);  /* le shell decide quoi en faire (buffer, backspace...) */
        }
    }
}

void keyboard_init(void) {
    irq_install_handler(1, keyboard_callback);
    pic_unmask_irq(1);   /* autorise les interruptions clavier (IRQ1) */
}
