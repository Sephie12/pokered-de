/*
 * gb_recomp_table.c - Zuordnung (Bank, Adresse) -> rekompilierte Funktion.
 * ROM0 hat eine eigene Tabelle, ROMX-Baenke werden nur angelegt, wenn
 * dort Code registriert wird.
 */
#include <stdlib.h>
#include "gb_recomp.h"

#define MAX_BANKS 512

static GBBlockFn *s_rom0;
static GBBlockFn *s_romx[MAX_BANKS];

void recomp_register(U8 bank, U16 addr, GBBlockFn fn)
{
    if (addr < 0x4000) {
        if (!s_rom0) {
            s_rom0 = (GBBlockFn *)calloc(0x4000, sizeof(GBBlockFn));
        }
        s_rom0[addr] = fn;
        return;
    }
    if (addr >= 0x8000) {
        return;
    }
    if (!s_romx[bank]) {
        s_romx[bank] = (GBBlockFn *)calloc(0x4000, sizeof(GBBlockFn));
    }
    s_romx[bank][addr - 0x4000] = fn;
}

void recomp_register_list(U8 bank, const RecompEntry *list)
{
    for (; list->fn; list++) {
        recomp_register(bank, list->addr, list->fn);
    }
}

GBBlockFn recomp_lookup(U16 pc)
{
    GBBlockFn *t;
    if (pc < 0x4000) {
        return s_rom0 ? s_rom0[pc] : NULL;
    }
    if (pc < 0x8000) {
        t = s_romx[g_gb.rom_bank];
        return t ? t[pc - 0x4000] : NULL;
    }
    return NULL;
}
