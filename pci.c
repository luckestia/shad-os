/* pci.c — Enumeration minimale du bus PCI, par acces direct aux ports
 * d'E/S 0xCF8 (adresse de configuration) et 0xCFC (donnees), mecanisme
 * standard present sur toute machine x86 depuis le PCI 2.x.
 *
 * On ne cherche qu'a localiser une carte reseau connue (voir net.c) :
 * pas de gestion generale des ponts PCI-PCI ni des peripheriques
 * multi-fonctions au-dela du strict necessaire.
 */
#include "kernel.h"
#include "io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

static uint32_t pci_make_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)(
        (1u << 31) |                 /* bit "enable" */
        ((uint32_t) bus  << 16) |
        ((uint32_t) slot << 11) |
        ((uint32_t) func << 8)  |
        (offset & 0xFC)
    );
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

static uint16_t pci_vendor_id(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint16_t)(pci_config_read32(bus, slot, func, 0x00) & 0xFFFF);
}

static uint16_t pci_device_id(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint16_t)(pci_config_read32(bus, slot, func, 0x00) >> 16);
}

/* Parcourt les 256 bus x 32 slots x 8 fonctions possibles (limite en
   pratique par les tres nombreux "0xFFFF" renvoyes par les slots vides,
   ce qui reste rapide car aucune veritable E/S materielle lente n'est
   impliquee, juste des acces port). Renvoie 1 et remplit bus/slot/func en sortie
   si le peripherique (vendor_id, device_id) est trouve, sinon 0. */
int pci_find_device(uint16_t vendor_id, uint16_t device_id,
                     uint8_t* out_bus, uint8_t* out_slot, uint8_t* out_func) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t slot = 0; slot < 32; slot++) {
            for (uint32_t func = 0; func < 8; func++) {
                if (pci_vendor_id((uint8_t) bus, (uint8_t) slot, (uint8_t) func) == vendor_id &&
                    pci_device_id((uint8_t) bus, (uint8_t) slot, (uint8_t) func) == device_id) {
                    if (out_bus) *out_bus = (uint8_t) bus;
                    if (out_slot) *out_slot = (uint8_t) slot;
                    if (out_func) *out_func = (uint8_t) func;
                    return 1;
                }
            }
        }
    }
    return 0;
}

uint32_t pci_get_bar0(uint8_t bus, uint8_t slot, uint8_t func) {
    return pci_config_read32(bus, slot, func, 0x10);
}

uint8_t pci_get_interrupt_line(uint8_t bus, uint8_t slot, uint8_t func) {
    return (uint8_t)(pci_config_read32(bus, slot, func, 0x3C) & 0xFF);
}

void pci_enable_bus_mastering(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd = pci_config_read32(bus, slot, func, 0x04);
    cmd |= (1 << 2);   /* bit "Bus Master Enable" */
    cmd |= (1 << 0);   /* bit "I/O Space Enable" (le RTL8139 utilise ses BAR en E/S) */
    pci_config_write32(bus, slot, func, 0x04, cmd);
}
