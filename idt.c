/* idt.c — Interrupt Descriptor Table : associe chaque numero d'interruption
 * a une routine assembleur (definie dans low_level.S).
 */
#include "kernel.h"
#include "pic.h"

struct idt_entry {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

#define IDT_ENTRIES 256
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;

extern void idt_flush(uint32_t idtp_addr);

/* Declarations des 32 stubs d'exceptions + 16 stubs d'IRQ (low_level.S) */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);  extern void isr3(void);
extern void isr4(void);  extern void isr5(void);  extern void isr6(void);  extern void isr7(void);
extern void isr8(void);  extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void); extern void isr15(void);
extern void isr16(void); extern void isr17(void); extern void isr18(void); extern void isr19(void);
extern void isr20(void); extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void); extern void isr27(void);
extern void isr28(void); extern void isr29(void); extern void isr30(void); extern void isr31(void);

extern void isr128(void);   /* int 0x80 : appels systeme (low_level.S) */

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);  extern void irq3(void);
extern void irq4(void);  extern void irq5(void);  extern void irq6(void);  extern void irq7(void);
extern void irq8(void);  extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void); extern void irq15(void);

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel      = sel;
    idt[num].always0  = 0;
    idt[num].flags    = flags;
}

/* Table des gestionnaires d'IRQ installables dynamiquement (drivers) */
static isr_t irq_routines[16] = { 0 };

void irq_install_handler(int irq, isr_t handler) {
    irq_routines[irq] = handler;
}

/* Messages d'exceptions CPU (pour affichage en cas de crash) */
static const char* exception_messages[32] = {
    "Division par zero", "Debug", "NMI", "Breakpoint",
    "Overflow", "Depassement de limite", "Opcode invalide", "Peripherique absent",
    "Double fault", "Depassement coprocesseur", "TSS invalide", "Segment absent",
    "Faute de pile", "Protection generale (GPF)", "Page fault", "Reservee",
    "Erreur x87", "Alignement", "Machine check", "SIMD",
    "Virtualisation", "Reservee", "Reservee", "Reservee",
    "Reservee", "Reservee", "Reservee", "Reservee",
    "Reservee", "Reservee", "Securite", "Reservee"
};

/* Appelee depuis low_level.S pour toute exception CPU (0-31) */
void isr_handler(struct registers* regs) {
    if (regs->int_no == 128) {
        syscall_dispatch(regs);   /* appel systeme (voir syscall.c) */
        return;
    }
    if (regs->int_no == 14) {
        page_fault_handler(regs);  /* traitement specifique (voir paging.c) */
        return;
    }
    if (regs->int_no < 32) {
        terminal_setcolor(vga_entry_color(VGA_WHITE, VGA_RED));
        terminal_writestring("\n*** EXCEPTION : ");
        terminal_writestring(exception_messages[regs->int_no]);
        terminal_writestring(" *** Systeme arrete.\n");
        terminal_setcolor(vga_entry_color(VGA_LIGHT_GREY, VGA_BLACK));
        for (;;) { __asm__ volatile ("cli; hlt"); }
    }
}

/* Appelee depuis low_level.S pour toute IRQ materielle (32-47) */
void irq_handler(struct registers* regs) {
    uint8_t irq = regs->int_no - 32;

    if (irq_routines[irq] != 0) {
        irq_routines[irq](regs);
    }

    pic_send_eoi(irq);
}

void idt_init(void) {
    idtp.limit = sizeof(struct idt_entry) * IDT_ENTRIES - 1;
    idtp.base  = (uint32_t) &idt;

    for (int i = 0; i < IDT_ENTRIES; i++) idt_set_gate(i, 0, 0, 0);

    pic_remap();

    /* 0x08 = segment code noyau, 0x8E = present + ring0 + interrupt gate 32bits */
    idt_set_gate(0, (uint32_t) isr0, 0x08, 0x8E);
    idt_set_gate(1, (uint32_t) isr1, 0x08, 0x8E);
    idt_set_gate(2, (uint32_t) isr2, 0x08, 0x8E);
    idt_set_gate(3, (uint32_t) isr3, 0x08, 0x8E);
    idt_set_gate(4, (uint32_t) isr4, 0x08, 0x8E);
    idt_set_gate(5, (uint32_t) isr5, 0x08, 0x8E);
    idt_set_gate(6, (uint32_t) isr6, 0x08, 0x8E);
    idt_set_gate(7, (uint32_t) isr7, 0x08, 0x8E);
    idt_set_gate(8, (uint32_t) isr8, 0x08, 0x8E);
    idt_set_gate(9, (uint32_t) isr9, 0x08, 0x8E);
    idt_set_gate(10, (uint32_t) isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t) isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t) isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t) isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t) isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t) isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t) isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t) isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t) isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t) isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t) isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t) isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t) isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t) isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t) isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t) isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t) isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t) isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t) isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t) isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t) isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t) isr31, 0x08, 0x8E);

    idt_set_gate(32, (uint32_t) irq0, 0x08, 0x8E);
    idt_set_gate(33, (uint32_t) irq1, 0x08, 0x8E);
    idt_set_gate(34, (uint32_t) irq2, 0x08, 0x8E);
    idt_set_gate(35, (uint32_t) irq3, 0x08, 0x8E);
    idt_set_gate(36, (uint32_t) irq4, 0x08, 0x8E);
    idt_set_gate(37, (uint32_t) irq5, 0x08, 0x8E);
    idt_set_gate(38, (uint32_t) irq6, 0x08, 0x8E);
    idt_set_gate(39, (uint32_t) irq7, 0x08, 0x8E);
    idt_set_gate(40, (uint32_t) irq8, 0x08, 0x8E);
    idt_set_gate(41, (uint32_t) irq9, 0x08, 0x8E);
    idt_set_gate(42, (uint32_t) irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t) irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t) irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t) irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t) irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t) irq15, 0x08, 0x8E);

    /* 0xEE = present + DPL3 + porte d'interruption 32 bits : DOIT avoir
       DPL3 (contrairement a toutes les autres portes ci-dessus, en DPL0)
       sinon l'instruction "int $0x80" executee en ring3 declenche une
       exception de protection generale (GPF) au lieu d'entrer dans le
       noyau : le CPU verifie que le CPL appelant est <= DPL de la porte. */
    idt_set_gate(128, (uint32_t) isr128, 0x08, 0xEE);

    idt_flush((uint32_t) &idtp);
}
