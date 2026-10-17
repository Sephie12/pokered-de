/*
 * gb_recomp.h - Makros fuer den vom Recompiler erzeugten C-Code.
 * Register liegen als lokale Variablen vor und werden beim Verlassen
 * der Funktion in g_gb.cpu zurueckgeschrieben.
 */
#ifndef GB_RECOMP_H
#define GB_RECOMP_H

#include "gb.h"
#include "gb_mem.h"
#include "gb_alu.h"

typedef struct {
    U16 addr;
    GBBlockFn fn;
} RecompEntry;

void recomp_register_list(U8 bank, const RecompEntry *list);

#define REGS_DECL U8 a, f, b, c, d, e, h, l, t8; U16 sp, t16; U32 lc;
#define REGS_LOAD \
    a = g_gb.cpu.a; f = g_gb.cpu.f; b = g_gb.cpu.b; c = g_gb.cpu.c; \
    d = g_gb.cpu.d; e = g_gb.cpu.e; h = g_gb.cpu.h; l = g_gb.cpu.l; \
    sp = g_gb.cpu.sp; t8 = 0; t16 = 0; lc = 0; (void)t8; (void)t16;
#define SAVE_REGS() do { \
    g_gb.cpu.a = a; g_gb.cpu.f = f; g_gb.cpu.b = b; g_gb.cpu.c = c; \
    g_gb.cpu.d = d; g_gb.cpu.e = e; g_gb.cpu.h = h; g_gb.cpu.l = l; \
    g_gb.cpu.sp = sp; } while (0)
#define FLUSH() do { g_gb.cycles += lc; lc = 0; } while (0)
#define EXIT(p) do { FLUSH(); SAVE_REGS(); g_gb.cpu.pc = (U16)(p); return; } while (0)

#define BC ((U16)((b << 8) | c))
#define DE ((U16)((d << 8) | e))
#define HL ((U16)((h << 8) | l))
#define SET_BC(v) do { U16 _v = (U16)(v); b = (U8)(_v >> 8); c = (U8)_v; } while (0)
#define SET_DE(v) do { U16 _v = (U16)(v); d = (U8)(_v >> 8); e = (U8)_v; } while (0)
#define SET_HL(v) do { U16 _v = (U16)(v); h = (U8)(_v >> 8); l = (U8)_v; } while (0)

/* Zyklen werden lokal gesammelt und nur vor zeitkritischen Zugriffen
 * (I/O, unbekannte Adressen), Deadline-Pruefungen und beim Verlassen
 * nach g_gb.cycles uebertragen. */
#define CYC(n) (lc += (n))
#define CHK(p) do { FLUSH(); if (GB_UNLIKELY((S32)(g_gb.cycles - g_gb.deadline) >= 0)) EXIT(p); } while (0)

/* Speicherzugriffe mit zur Laufzeit bekannter Adresse: ausgelagert */
#define RD(addr)      (g_gb.cycles += lc, lc = 0, gb_read((U16)(addr)))
#define WR(addr, v)   do { FLUSH(); gb_write((U16)(addr), (U8)(v)); } while (0)
#define RDH(n)        (g_gb.cycles += lc, lc = 0, gb_read((U16)(0xFF00 | (n))))
#define WRH(n, v)     do { FLUSH(); gb_write((U16)(0xFF00 | (n)), (U8)(v)); } while (0)
#define BANKCHK(bk, p) do { if (GB_UNLIKELY(g_gb.rom_bank != (bk))) EXIT(p); } while (0)

/* Stapel: schneller Pfad fuer WRAM, sonst allgemeiner Zugriff */
U16  gb_pop16(U16 sp);
void gb_push16(U16 sp, U16 v);
#define PUSH16(v) do { U16 _p = (U16)(v); sp = (U16)(sp - 2); \
    if (GB_LIKELY(sp >= 0xC000 && sp < 0xDFFF)) { g_gb.wram[sp & 0x1FFF] = (U8)_p; g_gb.wram[(sp + 1) & 0x1FFF] = (U8)(_p >> 8); } \
    else { FLUSH(); gb_push16(sp, _p); } } while (0)
#define POP16(dst) do { U16 _q; \
    if (GB_LIKELY(sp >= 0xC000 && sp < 0xDFFF)) { _q = (U16)(g_gb.wram[sp & 0x1FFF] | (g_gb.wram[(sp + 1) & 0x1FFF] << 8)); } \
    else { FLUSH(); _q = gb_pop16(sp); } \
    sp = (U16)(sp + 2); (dst) = _q; } while (0)

#endif
