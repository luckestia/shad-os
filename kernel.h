/* kernel.h — Declarations partagees entre les modules du kernel */
#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

/* --- Terminal / VGA --- */
enum vga_color {
    VGA_BLACK = 0,
    VGA_BLUE = 1,
    VGA_GREEN = 2,
    VGA_CYAN = 3,
    VGA_RED = 4,
    VGA_LIGHT_GREY = 7,
    VGA_DARK_GREY = 8,
    VGA_LIGHT_RED = 12,
    VGA_YELLOW = 14,
    VGA_WHITE = 15,
};

void terminal_initialize(void);
void terminal_putchar(char c);
void terminal_writestring(const char* str);
void terminal_setcolor(uint8_t color);
uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg);
void terminal_backspace(void);
void terminal_put_at(size_t row, size_t col, char c, uint8_t color);
void terminal_write_uint(uint32_t n);
void terminal_write_hex(uint32_t n);

/* --- GDT --- */
void gdt_init(void);

/* --- IDT / interruptions --- */
struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax; /* pusha */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss; /* pushes automatiques du CPU */
};

typedef void (*isr_t)(struct registers*);

void idt_init(void);
void irq_install_handler(int irq, isr_t handler);

/* --- PIC --- */
void pic_remap(void);
void pic_send_eoi(uint8_t irq);

/* --- Clavier --- */
void keyboard_init(void);

/* --- Shell --- */
void shell_init(void);
void shell_handle_char(char c);

/* --- Timer (PIT) --- */
void timer_init(uint32_t frequency_hz);
uint32_t timer_get_ticks(void);
uint32_t timer_get_seconds(void);

/* --- Pagination --- */
void paging_init(void);
void paging_identity_map_region(uint32_t phys_addr, uint32_t size);
void page_fault_handler(struct registers* regs);

/* --- Tas dynamique (malloc/free) --- */
void heap_init(void);
void* kmalloc(size_t size);
void kfree(void* ptr);
void heap_get_stats(size_t* total, size_t* used, size_t* free_bytes, int* free_blocks);
uint32_t heap_get_base(void);

/* --- Multitache (processus, changement de contexte) --- */
typedef void (*task_entry_t)(void);
void tasking_init(void);
int  task_create(task_entry_t entry);   /* renvoie le PID, ou -1 si echec */
void task_exit(void);
void schedule(void);
void tasking_tick(void);      /* appelee par le timer a chaque interruption */
void tasking_list(void);      /* affiche l'etat des taches (commande "ps") */
void tasking_set_paused(int paused);   /* gele/degele l'ordonnanceur (utilise par usermode.c) */

/* --- Systeme de fichiers RAM (ramfs) --- */
void ramfs_init(void);
int  ramfs_write(const char* name, const uint8_t* data, size_t size);
int  ramfs_read(const char* name, uint8_t** out_data, size_t* out_size);
int  ramfs_delete(const char* name);
void ramfs_list(void);
void memcpy_ramfs(uint8_t* dst, const uint8_t* src, size_t n);

/* --- Souris PS/2 --- */
void mouse_init(void);
void mouse_get_state(int* x, int* y, uint8_t* buttons);
int  mouse_is_ready(void);

/* --- Mode graphique (framebuffer Multiboot/VBE) --- */
void graphics_init(uint32_t mb_info_addr);
int  graphics_available(void);
uint32_t graphics_get_width(void);
uint32_t graphics_get_height(void);
void graphics_demo(void);

/* --- Appels systeme / mode utilisateur (ring3) --- */
void syscall_dispatch(struct registers* regs);
void usermode_run_demo(void);

/* --- PCI --- */
int      pci_find_device(uint16_t vendor_id, uint16_t device_id,
                          uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func);
uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
uint32_t pci_get_bar0(uint8_t bus, uint8_t slot, uint8_t func);
uint8_t  pci_get_interrupt_line(uint8_t bus, uint8_t slot, uint8_t func);
void     pci_enable_bus_mastering(uint8_t bus, uint8_t slot, uint8_t func);

/* --- Carte reseau RTL8139 --- */
int      rtl8139_init(void);
int      rtl8139_is_ready(void);
void     rtl8139_get_mac(uint8_t out_mac[6]);
uint32_t rtl8139_get_rx_count(void);
int      rtl8139_send(const uint8_t* data, uint32_t len);

/* --- Reseau (couche Ethernet/ARP minimale) --- */
void net_init(void);
int  net_is_ready(void);
int  net_send_arp_request(const uint8_t target_ip[4]);

/* --- Diagnostic serie (COM1), independant de l'affichage --- */
void serial_init(void);
void serial_putc(char c);
void serial_writestring(const char* s);
void serial_write_hex(uint32_t n);
int  serial_has_data(void);
char serial_read_char(void);

#endif
