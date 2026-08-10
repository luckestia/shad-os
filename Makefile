# Makefile — Compilation du kernel ShadestiaOS

CC      := gcc
AS      := as
LD      := ld

CFLAGS  := -m32 -ffreestanding -fno-stack-protector -fno-pie -fno-pic \
           -fno-asynchronous-unwind-tables -fno-unwind-tables -nostdlib \
           -Wall -Wextra -O2 -std=gnu11
ASFLAGS := --32
LDFLAGS := -m elf_i386 -T linker.ld -nostdlib -no-pie

OBJS := boot.o low_level.o kernel.o terminal.o gdt.o idt.o pic.o keyboard.o \
        string.o timer.o paging.o heap.o tasking.o shell.o \
        ramfs.o mouse.o graphics.o syscall.o usermode.o \
        pci.o rtl8139.o net.o serial.o

# OBJS_GFX : identiques a OBJS, sauf boot_gfx.o a la place de boot.o (seule
# difference : l'en-tete Multiboot demande un framebuffer graphique). Les
# deux variantes ne peuvent pas partager shadestia.bin : GRUB honore la
# requete video de l'en-tete AVANT meme de lancer le kernel, ce qui rend
# le mode texte legacy invisible - voir CORRECTIONS.md. D'ou deux binaires.
OBJS_GFX := $(patsubst boot.o,boot_gfx.o,$(OBJS))

all: shadestia.bin

boot.o: boot.S
	$(AS) $(ASFLAGS) boot.S -o boot.o

boot_gfx.o: boot_gfx.S
	$(AS) $(ASFLAGS) boot_gfx.S -o boot_gfx.o

low_level.o: low_level.S
	$(AS) $(ASFLAGS) low_level.S -o low_level.o

%.o: %.c kernel.h
	$(CC) $(CFLAGS) -c $< -o $@

shadestia.bin: linker.ld $(OBJS)
	$(LD) $(LDFLAGS) -o shadestia.bin $(OBJS)

shadestia-gfx.bin: linker.ld $(OBJS_GFX)
	$(LD) $(LDFLAGS) -o shadestia-gfx.bin $(OBJS_GFX)

check-multiboot: shadestia.bin
	grub-file --is-x86-multiboot shadestia.bin && echo "OK: Multiboot valide" || echo "ECHEC"

iso: shadestia.bin shadestia-gfx.bin
	mkdir -p isodir/boot/grub
	cp shadestia.bin isodir/boot/shadestia.bin
	cp shadestia-gfx.bin isodir/boot/shadestia-gfx.bin
	echo 'set timeout=2' > isodir/boot/grub/grub.cfg
	echo 'set default=0' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "ShadestiaOS" {' >> isodir/boot/grub/grub.cfg
	echo '    multiboot /boot/shadestia.bin' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	echo 'menuentry "ShadestiaOS (mode graphique, pour la commande gfx)" {' >> isodir/boot/grub/grub.cfg
	echo '    multiboot /boot/shadestia-gfx.bin' >> isodir/boot/grub/grub.cfg
	echo '}' >> isodir/boot/grub/grub.cfg
	grub-mkrescue -o shadestia.iso isodir

run: shadestia.bin
	qemu-system-i386 -kernel shadestia.bin

clean:
	rm -rf *.o shadestia.bin shadestia-gfx.bin shadestia.iso isodir

.PHONY: all clean iso run check-multiboot
