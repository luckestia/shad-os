# ShadestiaOS — Corrigé et validé (bootable)

## Résumé

Votre noyau était déjà **fonctionnellement complet** (GDT, IDT, PIC, timer PIT,
clavier, pagination, tas dynamique, multitâche préemptif, shell) et compilait
sans erreur. Mais il ne pouvait **pas booter du tout**.

## Le bug

Dans `boot.S`, l'en-tête Multiboot était déclaré ainsi :

```asm
.section .multiboot
```

Sans le flag `"a"` (alloc), l'assembleur ne marque pas cette section comme
chargeable en mémoire (`SHF_ALLOC`). Le linker l'excluait donc de tout
segment `PT_LOAD`, et elle se retrouvait repoussée à la fin du fichier
binaire — à l'offset `0x4004` (16 388 octets), bien au-delà des **8 Ko**
exigés par la spécification Multiboot pour que GRUB puisse trouver l'en-tête.

Vérifiable avant/après avec :
```bash
grub-file --is-x86-multiboot shadestia.bin
```
(échouait avant, réussit après le fix)

## Le fix (une ligne)

```asm
.section .multiboot, "a"
```

## Autre amélioration apportée

Le `grub.cfg` généré par `make iso` n'avait pas de `timeout`, donc l'ISO
restait bloquée sur le menu GRUB en attendant une touche. Ajout de
`set timeout=1` + `set default=0` pour un démarrage automatique.

## Preuves (dossier `screenshots/`)

1. `01_boot_direct_shell.png` — boot direct via `qemu-system-i386 -kernel`,
   toutes les étapes d'initialisation passent, shell `shadestia>` actif, tâches
   de démo visibles en bas d'écran.
2. `02_iso_grub_menu.png` — le vrai menu GRUB généré par `make iso`,
   confirmant que la structure ISO/Multiboot est valide.
3. `03_iso_boot_complet_shell.png` — boot complet depuis l'ISO (`-cdrom`),
   GRUB → noyau → shell, le scénario réel de bout en bout.

## Comment l'utiliser

```bash
make          # compile shadestia.bin
make check-multiboot   # vérifie l'en-tête Multiboot (doit dire "OK")
make run      # lance dans QEMU directement (boot rapide, sans GRUB)
make iso      # génère shadestia.iso (bootable sur clé USB réelle avec dd, ou dans une VM)
```

Pour tester l'ISO :
```bash
qemu-system-i386 -cdrom shadestia.iso
```

Pour écrire sur une vraie clé USB et tester sur du matériel physique
(⚠️ efface la clé) :
```bash
sudo dd if=shadestia.iso of=/dev/sdX bs=4M status=progress && sync
```

## État du projet — ce qui marche déjà

- [x] Boot Multiboot (GRUB) — **corrigé**
- [x] Terminal VGA texte avec couleurs
- [x] GDT
- [x] IDT + gestion des exceptions/IRQ
- [x] PIC remappé
- [x] Timer PIT (100 Hz)
- [x] Driver clavier (scancodes → shell)
- [x] Pagination (identity map 16 Mo)
- [x] Allocateur dynamique (kmalloc/kfree, 8 Mo, avec stats)
- [x] Multitâche préemptif (changement de contexte via le timer)
- [x] Shell avec 8 commandes (`help`, `clear`, `echo`, `uptime`, `meminfo`,
      `alloctest`, `ps`, `reboot`)

## Pistes pour aller plus loin (non implémenté, idées de suite)

- Système de fichiers (même minimal, en RAM — un "ramfs")
- Support de l'écran en mode graphique (VESA/VBE) en plus du mode texte
- Appels système (`syscall`/`int 0x80`) pour permettre du code en mode
  utilisateur (ring 3), séparé du kernel (ring 0)
- Chargement de programmes externes (format binaire simple, ou ELF)
- Pilote souris (PS/2)
- Networking basique (driver NE2000 ou RTL8139, pile TCP/IP minimale)
