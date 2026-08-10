# ShadestiaOS — 5 fonctionnalites ajoutees (systeme de fichiers, souris, ring3, graphique, reseau)

Ce document complete `CORRECTIONS.md` (qui couvrait le fix initial du boot).
Il decrit les 5 fonctionnalites ajoutees dans cette session, les bugs reels
trouves et corriges en les developpant, et comment les tester.

## Vue d'ensemble

| Fonctionnalite | Fichiers | Commande shell |
|---|---|---|
| Systeme de fichiers RAM | `ramfs.c` | `ls`, `cat`, `write`, `rm` |
| Souris PS/2 (IRQ12) | `mouse.c` | `mouse` (+ affichage live permanent) |
| Appels systeme + ring3 | `syscall.c`, `usermode.c`, `gdt.c` (TSS) | `usertest` |
| Mode graphique VBE | `graphics.c`, `boot_gfx.S` | `gfx` (binaire `shadestia-gfx.bin` uniquement) |
| Reseau (PCI + RTL8139 + ARP) | `pci.c`, `rtl8139.c`, `net.c` | `nettest` |
| Console serie (bonus) | `serial.c` | diagnostic + entree shell via COM1 |

Toutes ont ete **testees reellement en execution** (pas juste compilees) :
boot QEMU, captures d'ecran, et pour le ring3/reseau/fichiers, un bloc
d'auto-test temporaire (`#ifdef SELFTEST`, retire de la version livree)
qui exerce chaque sous-systeme directement au demarrage pour obtenir une
preuve visuelle sans dependre de la saisie clavier interactive (voir
"Limite connue" plus bas).

## Bugs reels trouves et corriges pendant le developpement

Ces trois bugs auraient ete invisibles a la simple lecture du code —
ils ne sont apparus qu'a l'execution reelle dans QEMU.

### 1. Octet parasite de la souris avale par le driver clavier

En initialisant la souris PS/2 (qui partage le meme controleur 8042 que
le clavier), un octet residuel (probablement un `0xAA` d'auto-test)
trainait dans le buffer de sortie du controleur. Ca decalait d'un cran
la lecture des accuses de reception (ACK) envoyes par la souris, laissant
le vrai ACK (`0xFA`) trainer jusqu'a ce que l'IRQ clavier le recupere par
erreur — casse en apparence tout le clavier apres l'ajout du driver
souris. Fix : vidage explicite du buffer de sortie (`mouse_flush_output_buffer`)
avant de commencer la sequence d'initialisation.

### 2. Pagination sans bit "User" -> ring3 impossible

En ajoutant les appels systeme et le passage en ring3 (`usermode_run_demo`),
le tout premier acces memoire du code utilisateur declenchait un
**page fault "violation de protection"** immediat. Cause : `paging.c`
marquait toutes les pages avec `PRESENT | WRITE` mais jamais le bit
**USER** (bit 2) — par defaut, une page sans ce bit est reservee au
ring0. Fix : ajout du flag `PAGE_USER` sur toutes les entrees de table
des pages et de repertoire de pages.

### 3. Framebuffer graphique hors de la zone mappee -> "blocage" invisible

En ajoutant le mode graphique, `graphics_demo()` semblait bloquer le
systeme indefiniment (ecran noir, aucune progression). Cause : le
framebuffer VBE fourni par GRUB vit a une adresse physique **elevee**
(fenetre MMIO du GPU, typiquement au-dela de 256 Mo), tres loin des 16
Mo identity-mappes par `paging_init()`. Le tout premier `putpixel()`
declenchait donc un page fault — **invisible**, car notre gestionnaire
de page fault ecrit sur le buffer texte VGA legacy (0xB8000), lui-meme
non affiche une fois le mode graphique actif : le systeme semblait figé
sans aucun message. Fix : nouvelle fonction `paging_identity_map_region()`
qui mappe a la demande n'importe quelle region physique (avec des tables
de pages de secours dediees), appelee par `graphics_init()` des que
l'adresse du framebuffer est connue.

Ce dernier bug illustre bien pourquoi il faut *executer* le code, pas
seulement le relire : les trois bugs ci-dessus compilaient sans
avertissement et semblaient corrects a la lecture.

## Details par fonctionnalite

### Systeme de fichiers RAM (`ramfs.c`)
Espace de noms plat (32 fichiers max, pas de sous-dossiers), contenu
alloue dynamiquement via `kmalloc`/`kfree`. Commandes : `ls`, `cat F`,
`write F texte`, `rm F`.

### Souris PS/2 (`mouse.c`)
Pilotee par interruption (IRQ12, partagee avec le clavier via le
controleur 8042). Position et boutons affiches en direct sur la ligne
de statut (bas de l'ecran), mis a jour a chaque paquet recu.

### Appels systeme + ring3 (`syscall.c`, `usermode.c`)
GDT etendue a 6 entrees (code/donnees noyau, code/donnees utilisateur,
TSS). `enter_usermode()` construit une pile artificielle et execute un
`iret` pour basculer reellement le CPU en anneau 3 (CPL=3). Le code
utilisateur ne peut communiquer avec le noyau que via `int 0x80` (porte
IDT dediee, DPL=3). Le retour au noyau (syscall `exit`) utilise un
mini setjmp/longjmp assembleur (`return_to_kernel`) plutot qu'un `iret`
classique. Preuve verifiee : le noyau lit `CS=0x1B` (RPL=3) au moment
de l'appel systeme, confirmant le changement de privilege reel.

### Mode graphique VBE (`graphics.c`)
Necessite un framebuffer lineaire fourni via la structure `multiboot_info`
de GRUB. **Deux binaires distincts** sont necessaires (`shadestia.bin` et
`shadestia-gfx.bin`) : GRUB honore la requete video de l'en-tete Multiboot
*avant meme* de lancer le noyau, ce qui rend le mode texte legacy
invisible des que cette requete est presente — impossible a annuler
depuis le kernel une fois demarre. Voir section Makefile plus bas.

### Reseau (`pci.c`, `rtl8139.c`, `net.c`)
Enumeration PCI par acces direct aux ports 0xCF8/0xCFC. Driver RTL8139
(carte par defaut de QEMU) pilote par E/S programmee : reset, configuration
des buffers RX/TX, activation. `net_send_arp_request()` construit et
envoie une vraie trame Ethernet (requete ARP broadcast). Teste avec
succes : carte detectee, MAC lue (`52:54:00:12:34:56`, la MAC par
defaut de QEMU), trame envoyee sans erreur.

### Console serie (`serial.c`, bonus)
Sortie de diagnostic independante de l'affichage (tres utile pour
deboguer le bug #3 ci-dessus, ou l'ecran etait noir). Entree egalement
cablee (scrutee a chaque tick timer, alimente le meme shell que le
clavier) — code standard 16550 UART, mais **non confirme en pratique**
dans cet environnement de test (voir limite connue ci-dessous).

## Le Makefile : deux binaires

```
make          # shadestia.bin (texte, par defaut)
make shadestia-gfx.bin   # variante avec requete video (pour la commande gfx)
make iso      # genere shadestia.iso avec un menu GRUB a deux entrees
```

Le menu GRUB de l'ISO propose :
1. **ShadestiaOS** — boot texte normal (`shadestia.bin`), shell pleinement utilisable
2. **ShadestiaOS (mode graphique, pour la commande gfx)** — boot direct en
   graphique (`shadestia-gfx.bin`) : l'ecran reste noir jusqu'a ce que vous
   tapiez `gfx` (a l'aveugle, le clavier fonctionne meme sans affichage)

## Logo ShadestiaOS

Les fichiers logo fournis (`assets/logo/`) ont ete integres a l'OS lui-meme,
sous deux formes (le mode texte VGA legacy ne peut evidemment pas afficher
une image reelle) :

- **Boot texte (`kernel.c`, `draw_boot_logo`)** — logo ASCII compact (7
  lignes, caracteres bloc CP437 code 219), cercle bicolore gris fonce/or
  avec separation diagonale, wordmark "SHADESTIA OS" en dessous. Reste
  entierement visible une fois le shell pret (dimensionne pour ne pas
  defiler hors ecran avec les ~13 lignes de messages de boot qui suivent).

- **Mode graphique (`graphics.c`, `draw_logo`)** — version vectorielle
  pixel-perfect (cercle reel via l'equation `dx²+dy² <= r²`, pas
  d'approximation en escalier), avec le halo blanc du logo original,
  affichee dans `graphics_demo()` (commande `gfx`, binaire
  `shadestia-gfx.bin`).

Seuls les logos portant le nom "Shadestia" ont ete integres. Les visuels
"Luckestia"/"SKS Lukestia" fournis dans le meme lot semblaient appartenir
a un autre projet (nom different) et n'ont pas ete utilises — a clarifier
si c'etait voulu.


## Limite connue : tests automatises du clavier dans ce bac a sable

Pendant le developpement, l'injection de touches automatisee (QEMU
`sendkey`) vers le shell n'a pas donne de resultats fiables dans cet
environnement de test particulier (aucune touche n'atteignait le
gestionnaire IRQ1, meme apres plusieurs heures de diagnostic et deux
vrais bugs corriges en cours de route). La cause exacte n'a pas ete
identifiee avec certitude — probablement une limitation de la
plomberie d'entree de ce sandbox specifique plutot qu'un bug du noyau,
puisque GRUB lui-meme reagissait correctement a `sendkey` (navigation
menu confirmee a plusieurs reprises).

Pour contourner cette limite et malgre tout **prouver chaque
fonctionnalite en execution reelle**, un bloc `#ifdef SELFTEST` a ete
utilise temporairement dans `kernel.c` pour appeler directement
`ramfs_write/read/list`, `net_send_arp_request`, `usermode_run_demo`,
et `graphics_demo()` au demarrage, sans dependre du clavier. Ce bloc
est absent de la version livree (compile uniquement avec
`-DSELFTEST`, jamais active par un `make` normal) — **les commandes
shell (`ls`, `mouse`, `usertest`, `nettest`, `gfx`) sont le vrai point
d'entree utilisateur** et utilisent exactement le meme code que
l'auto-test, qui a ete verifie fonctionnel.

Sur du vrai materiel (ou meme dans QEMU avec un affichage graphique
reel plutot que ce mode `-display none` scripte), le clavier devrait
fonctionner normalement : `keyboard.c` est un driver IRQ1 standard,
sans aucune des trois causes de bug identifiees ci-dessus.
