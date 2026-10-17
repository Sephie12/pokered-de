/*
 * gb_alu.h - SM83-Arithmetik, gemeinsam genutzt von Interpreter und
 * rekompiliertem Code. Alle Funktionen arbeiten auf Werten und einem
 * Zeiger auf F, damit der Compiler im rekompilierten Code lokale
 * Register in CPU-Registern halten kann.
 */
#ifndef GB_ALU_H
#define GB_ALU_H

#include "gb.h"

GB_INLINE U8 alu_add(U8 a, U8 v, U8 *f)
{
    U32 r;
    r = (U32)a + v;
    *f = (U8)((((r & 0xFF) == 0) ? FLAG_Z : 0)
            | (((a & 0xF) + (v & 0xF)) > 0xF ? FLAG_H : 0)
            | (r > 0xFF ? FLAG_C : 0));
    return (U8)r;
}

GB_INLINE U8 alu_adc(U8 a, U8 v, U8 *f)
{
    U32 c, r;
    c = (*f & FLAG_C) ? 1 : 0;
    r = (U32)a + v + c;
    *f = (U8)((((r & 0xFF) == 0) ? FLAG_Z : 0)
            | (((a & 0xF) + (v & 0xF) + c) > 0xF ? FLAG_H : 0)
            | (r > 0xFF ? FLAG_C : 0));
    return (U8)r;
}

GB_INLINE U8 alu_sub(U8 a, U8 v, U8 *f)
{
    U8 r;
    r = (U8)(a - v);
    *f = (U8)((r == 0 ? FLAG_Z : 0) | FLAG_N
            | ((a & 0xF) < (v & 0xF) ? FLAG_H : 0)
            | (a < v ? FLAG_C : 0));
    return r;
}

GB_INLINE U8 alu_sbc(U8 a, U8 v, U8 *f)
{
    S32 c, r;
    c = (*f & FLAG_C) ? 1 : 0;
    r = (S32)a - v - c;
    *f = (U8)((((U8)r) == 0 ? FLAG_Z : 0) | FLAG_N
            | (((S32)(a & 0xF) - (v & 0xF) - c) < 0 ? FLAG_H : 0)
            | (r < 0 ? FLAG_C : 0));
    return (U8)r;
}

GB_INLINE U8 alu_and(U8 a, U8 v, U8 *f)
{
    U8 r;
    r = (U8)(a & v);
    *f = (U8)((r == 0 ? FLAG_Z : 0) | FLAG_H);
    return r;
}

GB_INLINE U8 alu_xor(U8 a, U8 v, U8 *f)
{
    U8 r;
    r = (U8)(a ^ v);
    *f = (U8)(r == 0 ? FLAG_Z : 0);
    return r;
}

GB_INLINE U8 alu_or(U8 a, U8 v, U8 *f)
{
    U8 r;
    r = (U8)(a | v);
    *f = (U8)(r == 0 ? FLAG_Z : 0);
    return r;
}

GB_INLINE void alu_cp(U8 a, U8 v, U8 *f)
{
    (void)alu_sub(a, v, f);
}

GB_INLINE U8 alu_inc(U8 v, U8 *f)
{
    U8 r;
    r = (U8)(v + 1);
    *f = (U8)((*f & FLAG_C) | (r == 0 ? FLAG_Z : 0) | ((r & 0xF) == 0 ? FLAG_H : 0));
    return r;
}

GB_INLINE U8 alu_dec(U8 v, U8 *f)
{
    U8 r;
    r = (U8)(v - 1);
    *f = (U8)((*f & FLAG_C) | (r == 0 ? FLAG_Z : 0) | FLAG_N | ((r & 0xF) == 0xF ? FLAG_H : 0));
    return r;
}

GB_INLINE U16 alu_add16(U16 hl, U16 v, U8 *f)
{
    U32 r;
    r = (U32)hl + v;
    *f = (U8)((*f & FLAG_Z)
            | (((hl & 0xFFF) + (v & 0xFFF)) > 0xFFF ? FLAG_H : 0)
            | (r > 0xFFFF ? FLAG_C : 0));
    return (U16)r;
}

/* SP + e8 (fuer ADD SP,e und LD HL,SP+e) */
GB_INLINE U16 alu_addsp(U16 sp, S8 e, U8 *f)
{
    U16 v;
    v = (U16)(S32)e;
    *f = (U8)((((sp & 0xF) + (v & 0xF)) > 0xF ? FLAG_H : 0)
            | (((sp & 0xFF) + (v & 0xFF)) > 0xFF ? FLAG_C : 0));
    return (U16)(sp + v);
}

GB_INLINE U8 alu_daa(U8 a, U8 *f)
{
    U32 corr, carry;
    corr = 0;
    carry = (*f & FLAG_C) ? 1 : 0;
    if (!(*f & FLAG_N)) {
        if ((*f & FLAG_H) || (a & 0xF) > 9) {
            corr |= 0x06;
        }
        if (carry || a > 0x99) {
            corr |= 0x60;
            carry = 1;
        }
        a = (U8)(a + corr);
    } else {
        if (*f & FLAG_H) {
            corr |= 0x06;
        }
        if (carry) {
            corr |= 0x60;
        }
        a = (U8)(a - corr);
    }
    *f = (U8)((*f & FLAG_N) | (a == 0 ? FLAG_Z : 0) | (carry ? FLAG_C : 0));
    return a;
}

/* Rotationen auf A (RLCA etc.: Z immer 0) */
GB_INLINE U8 alu_rlca(U8 a, U8 *f)
{
    U8 c;
    c = (U8)(a >> 7);
    *f = (U8)(c ? FLAG_C : 0);
    return (U8)((a << 1) | c);
}

GB_INLINE U8 alu_rrca(U8 a, U8 *f)
{
    U8 c;
    c = (U8)(a & 1);
    *f = (U8)(c ? FLAG_C : 0);
    return (U8)((a >> 1) | (c << 7));
}

GB_INLINE U8 alu_rla(U8 a, U8 *f)
{
    U8 c;
    c = (U8)(a >> 7);
    a = (U8)((a << 1) | ((*f & FLAG_C) ? 1 : 0));
    *f = (U8)(c ? FLAG_C : 0);
    return a;
}

GB_INLINE U8 alu_rra(U8 a, U8 *f)
{
    U8 c;
    c = (U8)(a & 1);
    a = (U8)((a >> 1) | ((*f & FLAG_C) ? 0x80 : 0));
    *f = (U8)(c ? FLAG_C : 0);
    return a;
}

/* CB-Prefix: op = 0..7 (rlc rrc rl rr sla sra swap srl) */
GB_INLINE U8 alu_cb_rot(int op, U8 v, U8 *f)
{
    U8 r, c;
    switch (op) {
    case 0: c = (U8)(v >> 7); r = (U8)((v << 1) | c); break;
    case 1: c = (U8)(v & 1); r = (U8)((v >> 1) | (c << 7)); break;
    case 2: c = (U8)(v >> 7); r = (U8)((v << 1) | ((*f & FLAG_C) ? 1 : 0)); break;
    case 3: c = (U8)(v & 1); r = (U8)((v >> 1) | ((*f & FLAG_C) ? 0x80 : 0)); break;
    case 4: c = (U8)(v >> 7); r = (U8)(v << 1); break;
    case 5: c = (U8)(v & 1); r = (U8)((v >> 1) | (v & 0x80)); break;
    case 6: c = 0; r = (U8)((v >> 4) | (v << 4)); break;
    default: c = (U8)(v & 1); r = (U8)(v >> 1); break;
    }
    *f = (U8)((r == 0 ? FLAG_Z : 0) | (c ? FLAG_C : 0));
    return r;
}

GB_INLINE void alu_bit(int bit, U8 v, U8 *f)
{
    *f = (U8)((*f & FLAG_C) | FLAG_H | ((v & (1 << bit)) ? 0 : FLAG_Z));
}

#endif
