/* net.c — Couche reseau minimale : construit et envoie une requete ARP
 * ("qui a cette IP ?") en trame Ethernet brute, via le driver RTL8139.
 * Sert de preuve de fonctionnement de bout en bout de la pile reseau :
 * PCI -> carte detectee -> trame construite -> transmise sur le fil.
 *
 * N'implemente PAS une vraie pile TCP/IP (pas de sockets, pas d'IP
 * routee) : c'est volontairement le strict minimum pour demontrer que
 * le materiel reseau est reellement pilote.
 */
#include "kernel.h"

static int net_ready = 0;

void net_init(void) {
    net_ready = rtl8139_init();
}

int net_is_ready(void) { return net_ready; }

static void write_be16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t) v; }

/* Envoie une requete ARP broadcast "Who has <target_ip>? Tell <sender_ip>".
   IPs au format 4 octets (ex: {10,0,2,2}). Renvoie 0 si la trame a ete
   remise a la carte pour transmission, -1 si la carte n'est pas prete. */
int net_send_arp_request(const uint8_t target_ip[4]) {
    if (!net_ready) return -1;

    uint8_t src_mac[6];
    rtl8139_get_mac(src_mac);

    /* IP source arbitraire (pas de vraie pile IP configuree) : on utilise
       une adresse plausible sur le sous-reseau par defaut de QEMU (10.0.2.x). */
    uint8_t sender_ip[4] = { 10, 0, 2, 15 };

    uint8_t frame[42];   /* 14 (Ethernet) + 28 (ARP) */

    /* En-tete Ethernet */
    for (int i = 0; i < 6; i++) frame[i] = 0xFF;              /* destination : broadcast */
    for (int i = 0; i < 6; i++) frame[6 + i] = src_mac[i];    /* source : notre MAC */
    write_be16(frame + 12, 0x0806);                            /* ethertype : ARP */

    /* Paquet ARP */
    uint8_t* arp = frame + 14;
    write_be16(arp + 0, 1);        /* hardware type : Ethernet */
    write_be16(arp + 2, 0x0800);   /* protocol type : IPv4 */
    arp[4] = 6;                     /* hardware address length */
    arp[5] = 4;                     /* protocol address length */
    write_be16(arp + 6, 1);        /* opcode : requete (1) */
    for (int i = 0; i < 6; i++) arp[8 + i] = src_mac[i];       /* MAC emetteur */
    for (int i = 0; i < 4; i++) arp[14 + i] = sender_ip[i];    /* IP emetteur */
    for (int i = 0; i < 6; i++) arp[18 + i] = 0x00;             /* MAC cible : inconnue (c'est le but de l'ARP) */
    for (int i = 0; i < 4; i++) arp[24 + i] = target_ip[i];     /* IP cible */

    return rtl8139_send(frame, sizeof(frame));
}
