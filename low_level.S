/* low_level.S — Routines bas niveau : chargement GDT/IDT, stubs d'interruptions.
 * Syntaxe GNU AS (AT&T).
 */

.section .text

/* ================= GDT ================= */
.global gdt_flush
gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)

    mov $0x10, %ax    /* 0x10 = offset du segment donnees noyau (entree 2 * 8) */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss

    jmp $0x08, $.flush   /* 0x08 = offset du segment code noyau (entree 1 * 8) */
.flush:
    ret

/* ================= IDT ================= */
.global idt_flush
idt_flush:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

/* ================= TSS ================= */
.global tss_flush
tss_flush:
    mov $0x2B, %ax    /* selecteur TSS = entree 5 * 8 = 0x28, RPL 3 par convention */
    ltr %ax
    ret

/* ================= Macros de generation des stubs ================= */

/* Exceptions SANS code d'erreur pousse automatiquement par le CPU */
.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    cli
    push $0            /* code d'erreur factice pour garder une structure uniforme */
    push $\num
    jmp isr_common_stub
.endm

/* Exceptions AVEC code d'erreur pousse automatiquement par le CPU */
.macro ISR_ERRCODE num
.global isr\num
isr\num:
    cli
    push $\num
    jmp isr_common_stub
.endm

.macro IRQ num, int_no
.global irq\num
irq\num:
    cli
    push $0
    push $\int_no
    jmp irq_common_stub
.endm

/* --- Exceptions CPU 0-31 --- */
ISR_NOERRCODE 0    /* Division par zero */
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6    /* Opcode invalide */
ISR_NOERRCODE 7
ISR_ERRCODE   8    /* Double fault */
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13   /* General Protection Fault */
ISR_ERRCODE   14   /* Page Fault */
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

/* --- Interruption logicielle 0x80 (128) : appels systeme.
   Meme mecanisme que les exceptions (structure "registers" uniforme),
   route vers syscall_dispatch() depuis isr_handler() (voir idt.c). --- */
ISR_NOERRCODE 128

/* --- IRQ materielles 0-15, remappees sur les vecteurs 32-47 --- */
IRQ 0, 32   /* Timer (PIT) */
IRQ 1, 33   /* Clavier */
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

/* ================= Stubs communs ================= */
.extern isr_handler
.extern irq_handler

isr_common_stub:
    pusha              /* sauvegarde eax,ecx,edx,ebx,esp,ebp,esi,edi */

    mov %ds, %ax
    push %eax          /* sauvegarde le segment de donnees */

    mov $0x10, %ax     /* charge le segment de donnees noyau pour le code C */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp          /* passe un pointeur vers struct registers en argument */
    call isr_handler
    add $4, %esp

    pop %eax           /* restaure le segment de donnees d'origine */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa
    add $8, %esp       /* nettoie int_no + err_code empiles */
    sti
    iret

irq_common_stub:
    pusha

    mov %ds, %ax
    push %eax

    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp
    call irq_handler
    add $4, %esp

    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa
    add $8, %esp
    sti
    iret

/* ================= Changement de contexte (multitache) ================= */
/* void switch_to(uint32_t* old_esp_ptr, uint32_t new_esp);
 *
 * Technique symetrique classique : on sauvegarde les registres "callee-saved"
 * (ebp, ebx, esi, edi — ceux que l'appelant suppose preserves par convention
 * cdecl) sur la pile COURANTE, on note l'ESP resultant dans *old_esp_ptr,
 * puis on bascule ESP vers new_esp (la pile d'une autre tache) et on restaure
 * SES registres callee-saved avant de faire "ret".
 *
 * Le "ret" final ne revient donc PAS dans la fonction appelante d'origine :
 * il saute a l'adresse de retour empilee sur la pile de la NOUVELLE tache —
 * soit le point ou cette tache avait elle-meme appele switch_to() la derniere
 * fois (elle reprend exactement la ou elle s'etait arretee), soit une adresse
 * "fabriquee" par task_create() pour une tache qui n'a encore jamais tourne. */
.global switch_to
switch_to:
    push %ebp
    push %ebx
    push %esi
    push %edi

    mov 20(%esp), %eax   /* 1er argument : old_esp_ptr (4 pushes + retaddr = 20) */
    mov %esp, (%eax)     /* *old_esp_ptr = esp courant (sauvegarde la tache sortante) */

    mov 24(%esp), %esp   /* 2e argument : new_esp -> bascule sur la nouvelle pile */

    pop %edi
    pop %esi
    pop %ebx
    pop %ebp
    ret

/* ================= Passage en mode utilisateur (ring 3) ================= */
/*
 * void enter_usermode(void (*entry)(void), uint32_t user_stack_top);
 *
 * Construit "a la main" la pile que le CPU s'attend a trouver pour un
 * retour d'interruption (iret) AVEC changement de privilege, puis execute
 * ce iret : le CPU bascule alors CS/EIP/SS/ESP/EFLAGS d'un seul coup vers
 * le code utilisateur, en ring 3. Avant km_esp/km_ebp : on sauvegarde la
 * position exacte de la pile noyau ICI (avant tout push), pour pouvoir
 * y revenir directement plus tard via return_to_kernel(), sans jamais
 * "vraiment" retourner de cette fonction par un chemin normal.
 */
.global enter_usermode
.extern km_esp
.extern km_ebp
enter_usermode:
    mov 4(%esp), %eax    /* 1er argument : point d'entree */
    mov 8(%esp), %ecx    /* 2e argument : sommet de la pile utilisateur */

    mov %esp, km_esp     /* point de reprise pour return_to_kernel() */
    mov %ebp, km_ebp

    mov $0x23, %dx        /* 0x20 (donnees utilisateur) | RPL 3 */
    mov %dx, %ds
    mov %dx, %es
    mov %dx, %fs
    mov %dx, %gs

    push $0x23             /* SS utilisateur */
    push %ecx                /* ESP utilisateur */
    pushf
    pop %edx
    or $0x200, %edx           /* force IF=1 (interruptions actives en ring3) */
    push %edx                  /* EFLAGS */
    push $0x1B                  /* CS utilisateur = 0x18 | RPL 3 */
    push %eax                    /* EIP = point d'entree */
    iret

/* void return_to_kernel(void); — jamais appelee "normalement" : c'est le
   syscall exit() (voir syscall.c) qui l'invoque pour annuler la pile
   d'interruption en cours et reprendre directement l'execution du noyau
   juste apres l'appel a enter_usermode(), comme si celle-ci avait fait
   un simple "ret". */
.global return_to_kernel
return_to_kernel:
    mov km_esp, %esp
    mov km_ebp, %ebp
    ret

.section .data
km_esp: .long 0
km_ebp: .long 0
