#ifndef ASM_H
#define ASM_H

#include <kstd/asm.h>

void hlt() {
    asm("hlt");
}

void cli() {
    asm("cli");
}

void sti() {
    asm("sti");
}

#endif