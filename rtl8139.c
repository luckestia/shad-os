/* rtl8139.c — Driver pour la carte reseau Realtek RTL8139 (tres repandue
 * en emulation : c'est le modele par defaut de QEMU/VirtualBox), pilotee
 * entierement par E/S programmee (pas de MMIO) via son BAR0.
 *
 * Registres utilises (offsets depuis le BAR0, cf. datasheet RTL8139) :
 *   0x00-0x05  IDR0-5      Adresse MAC de la carte (lecture)
 *   0x10-0x1C  TSD0-3      Statut de transmission (4 descripteurs, tour de role)
 *   0x20-0x2C  TSAD0-3     Adresse physique du buffer a transmettre
 *   0x30       RBSTART     Adresse physique du buffer de reception (anneau)
 *   0x37       CMD         Commandes (reset, activation RX/TX)
 *   0x38       CAPR        Position de lecture courante dans l'anneau RX
 *   0x3C       IMR         Masque des interruptions activees
 *   0x3E       ISR         Interruptions en attente (a acquitter en ecrivant dedans)
 *   0x40       TCR         Configuration de la transmission
 *   0x44       RCR         Configuration de la reception
 *   0x52       CONFIG1     Gestion d'alimentation (0 = carte allumee)
 */
#include "kernel.h"
#include "io.h"
#include "pic.h"

#define RTL_VENDOR_ID 0x10EC
#define RTL_DEVICE_ID 0x8139

#define REG_MAC0      0x00
#define REG_TSD0      0x10
#define REG_TSAD0     0x20
#define REG_RBSTART   0x30
#define REG_CMD       0x37
#define REG_CAPR      0x38
#define REG_IMR       0x3C
#define REG_ISR       0x3E
#define REG_TCR       0x40
#define REG_RCR       0x44
#define REG_CONFIG1   0x52

#define RX_BUF_SIZE (8192 + 16 + 1500)   /* +16 marge d'en-tete, +1500 pad (bit WRAP) */
#define TX_BUF_SIZE 1600

static uint32_t io_base = 0;
static uint8_t  mac[6];
static uint8_t* rx_buffer = NULL;
static uint8_t* tx_buffer = NULL;   /* un seul buffer TX reutilise (suffisant pour la demo) */
static uint32_t rx_offset = 0;
static int      tx_cur = 0;
static int      rtl_ready = 0;
static uint32_t rx_packets_seen = 0;

static void rtl_irq_handler(struct registers* regs) {
    (void) regs;
    uint16_t status = inw(io_base + REG_ISR);
    outw(io_base + REG_ISR, status);   /* acquitte : ecrire les bits remet a zero */

    if (status & 0x01) {   /* ROK : reception OK, au moins un paquet dispo */
        uint32_t guard = 0;
        while (!(inb(io_base + REG_CMD) & 0x01) && guard < 64) {  /* bit0=BUFE : 0 = pas vide */
            uint16_t* hdr = (uint16_t*)(rx_buffer + rx_offset);
            uint16_t length = hdr[1];

            if (length < 4 || length > 1600) break;  /* trame corrompue : on abandonne par securite */

            rx_packets_seen++;

            rx_offset = (rx_offset + length + 4 + 3) & ~3u;
            if (rx_offset >= 8192) rx_offset -= 8192;
            outw(io_base + REG_CAPR, (uint16_t)(rx_offset - 16));
            guard++;
        }
    }
}

int rtl8139_init(void) {
    uint8_t bus, slot, func;
    if (!pci_find_device(RTL_VENDOR_ID, RTL_DEVICE_ID, &bus, &slot, &func)) {
        return 0;   /* pas de carte RTL8139 (normal hors QEMU/VirtualBox par defaut) */
    }

    pci_enable_bus_mastering(bus, slot, func);
    io_base = pci_get_bar0(bus, slot, func) & 0xFFFFFFFC;  /* les 2 bits de poids faible indiquent E/S vs MMIO */

    outb(io_base + REG_CONFIG1, 0x00);          /* allume la carte */

    outb(io_base + REG_CMD, 0x10);              /* reset logiciel */
    uint32_t guard = 1000000;
    while ((inb(io_base + REG_CMD) & 0x10) && guard--) { }

    for (int i = 0; i < 6; i++) mac[i] = inb(io_base + REG_MAC0 + i);

    rx_buffer = (uint8_t*) kmalloc(RX_BUF_SIZE);
    tx_buffer = (uint8_t*) kmalloc(TX_BUF_SIZE);
    if (!rx_buffer || !tx_buffer) return 0;
    rx_offset = 0;

    outl(io_base + REG_RBSTART, (uint32_t) rx_buffer);

    outw(io_base + REG_IMR, 0x0005);            /* ROK (bit0) + TOK (bit2) */

    /* RCR : accepte tout (AAP|APM|AM|AB = 0x0F) + bit WRAP (0x80) pour
       simplifier la lecture de l'anneau (necessite le pad +1500 ci-dessus). */
    outl(io_base + REG_RCR, 0x0000008F);

    outb(io_base + REG_CMD, 0x0C);              /* active RX (bit3) + TX (bit2) */

    uint8_t irq_line = pci_get_interrupt_line(bus, slot, func);
    irq_install_handler(irq_line, rtl_irq_handler);
    pic_unmask_irq(irq_line);

    rtl_ready = 1;
    return 1;
}

int rtl8139_is_ready(void) { return rtl_ready; }

void rtl8139_get_mac(uint8_t out_mac[6]) {
    for (int i = 0; i < 6; i++) out_mac[i] = mac[i];
}

uint32_t rtl8139_get_rx_count(void) { return rx_packets_seen; }

/* Envoie une trame Ethernet brute (len doit inclure l'en-tete Ethernet).
   Renvoie 0 si succes, -1 si la carte n'est pas prete ou la trame trop grande. */
int rtl8139_send(const uint8_t* data, uint32_t len) {
    if (!rtl_ready || len > TX_BUF_SIZE) return -1;

    for (uint32_t i = 0; i < len; i++) tx_buffer[i] = data[i];
    uint32_t padded_len = len < 60 ? 60 : len;   /* taille minimale Ethernet (hors CRC, ajoute par le materiel) */
    if (len < 60) {
        for (uint32_t i = len; i < 60; i++) tx_buffer[i] = 0;
    }

    outl(io_base + REG_TSAD0 + tx_cur * 4, (uint32_t) tx_buffer);
    outl(io_base + REG_TSD0 + tx_cur * 4, padded_len);  /* ecrire la longueur declenche l'envoi */

    tx_cur = (tx_cur + 1) % 4;
    return 0;
}
