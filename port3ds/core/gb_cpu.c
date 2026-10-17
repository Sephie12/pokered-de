/*
 * gb_cpu.c - SM83-Interpreter.
 *
 * Wird fuer Code verwendet, der nicht rekompiliert vorliegt (HRAM-DMA-
 * Routine, unentdeckte Sprungziele) und dient als Referenz fuer den
 * Differenztest. Gemeinsame Arithmetik in gb_alu.h.
 */
#include "gb.h"
#include "gb_mem.h"
#include "gb_alu.h"

#define CPU (g_gb.cpu)

static U8 fetch8(void)
{
    U8 v;
    v = mem_rd(CPU.pc);
    CPU.pc++;
    return v;
}

static U16 fetch16(void)
{
    U16 lo;
    lo = fetch8();
    return (U16)(lo | (fetch8() << 8));
}

static void push16(U16 v)
{
    CPU.sp = (U16)(CPU.sp - 2);
    mem_wr(CPU.sp, (U8)(v & 0xFF));
    mem_wr((U16)(CPU.sp + 1), (U8)(v >> 8));
}

static U16 pop16(void)
{
    U16 v;
    v = mem_rd(CPU.sp);
    v = (U16)(v | (mem_rd((U16)(CPU.sp + 1)) << 8));
    CPU.sp = (U16)(CPU.sp + 2);
    return v;
}

#define BC ((U16)((CPU.b << 8) | CPU.c))
#define DE ((U16)((CPU.d << 8) | CPU.e))
#define HL ((U16)((CPU.h << 8) | CPU.l))
#define SET_BC(v) do { U16 _v = (v); CPU.b = (U8)(_v >> 8); CPU.c = (U8)_v; } while (0)
#define SET_DE(v) do { U16 _v = (v); CPU.d = (U8)(_v >> 8); CPU.e = (U8)_v; } while (0)
#define SET_HL(v) do { U16 _v = (v); CPU.h = (U8)(_v >> 8); CPU.l = (U8)_v; } while (0)

static U8 get_r(int i)
{
    switch (i) {
    case 0: return CPU.b;
    case 1: return CPU.c;
    case 2: return CPU.d;
    case 3: return CPU.e;
    case 4: return CPU.h;
    case 5: return CPU.l;
    case 6: return mem_rd(HL);
    default: return CPU.a;
    }
}

static void set_r(int i, U8 v)
{
    switch (i) {
    case 0: CPU.b = v; break;
    case 1: CPU.c = v; break;
    case 2: CPU.d = v; break;
    case 3: CPU.e = v; break;
    case 4: CPU.h = v; break;
    case 5: CPU.l = v; break;
    case 6: mem_wr(HL, v); break;
    default: CPU.a = v; break;
    }
}

static int cond(int c)
{
    switch (c) {
    case 0: return !(CPU.f & FLAG_Z);
    case 1: return (CPU.f & FLAG_Z) != 0;
    case 2: return !(CPU.f & FLAG_C);
    default: return (CPU.f & FLAG_C) != 0;
    }
}

static int alu_op(int op, U8 v)
{
    switch (op) {
    case 0: CPU.a = alu_add(CPU.a, v, &CPU.f); break;
    case 1: CPU.a = alu_adc(CPU.a, v, &CPU.f); break;
    case 2: CPU.a = alu_sub(CPU.a, v, &CPU.f); break;
    case 3: CPU.a = alu_sbc(CPU.a, v, &CPU.f); break;
    case 4: CPU.a = alu_and(CPU.a, v, &CPU.f); break;
    case 5: CPU.a = alu_xor(CPU.a, v, &CPU.f); break;
    case 6: CPU.a = alu_or(CPU.a, v, &CPU.f); break;
    default: alu_cp(CPU.a, v, &CPU.f); break;
    }
    return 0;
}

static int exec_cb(void)
{
    U8 op, v;
    int r, x, y;
    op = fetch8();
    r = op & 7;
    x = op >> 6;
    y = (op >> 3) & 7;
    v = get_r(r);
    switch (x) {
    case 0:
        set_r(r, alu_cb_rot(y, v, &CPU.f));
        return r == 6 ? 16 : 8;
    case 1:
        alu_bit(y, v, &CPU.f);
        return r == 6 ? 12 : 8;
    case 2:
        set_r(r, (U8)(v & ~(1 << y)));
        return r == 6 ? 16 : 8;
    default:
        set_r(r, (U8)(v | (1 << y)));
        return r == 6 ? 16 : 8;
    }
}

int gb_interp_step(void)
{
    U8 op;
    int cyc;

    g_gb.interp_steps++;
    op = fetch8();
    cyc = 4;

    if (op >= 0x40 && op < 0x80) {
        if (op == 0x76) {
            CPU.halted = 1;
            g_gb.cycles += 4;
            return 4;
        }
        set_r((op >> 3) & 7, get_r(op & 7));
        cyc = ((op & 7) == 6 || ((op >> 3) & 7) == 6) ? 8 : 4;
        g_gb.cycles += (U32)cyc;
        return cyc;
    }
    if (op >= 0x80 && op < 0xC0) {
        alu_op((op >> 3) & 7, get_r(op & 7));
        cyc = (op & 7) == 6 ? 8 : 4;
        g_gb.cycles += (U32)cyc;
        return cyc;
    }

    switch (op) {
    case 0x00: break;
    case 0x01: SET_BC(fetch16()); cyc = 12; break;
    case 0x11: SET_DE(fetch16()); cyc = 12; break;
    case 0x21: SET_HL(fetch16()); cyc = 12; break;
    case 0x31: CPU.sp = fetch16(); cyc = 12; break;
    case 0x02: mem_wr(BC, CPU.a); cyc = 8; break;
    case 0x12: mem_wr(DE, CPU.a); cyc = 8; break;
    case 0x22: mem_wr(HL, CPU.a); SET_HL(HL + 1); cyc = 8; break;
    case 0x32: mem_wr(HL, CPU.a); SET_HL(HL - 1); cyc = 8; break;
    case 0x0A: CPU.a = mem_rd(BC); cyc = 8; break;
    case 0x1A: CPU.a = mem_rd(DE); cyc = 8; break;
    case 0x2A: CPU.a = mem_rd(HL); SET_HL(HL + 1); cyc = 8; break;
    case 0x3A: CPU.a = mem_rd(HL); SET_HL(HL - 1); cyc = 8; break;
    case 0x03: SET_BC(BC + 1); cyc = 8; break;
    case 0x13: SET_DE(DE + 1); cyc = 8; break;
    case 0x23: SET_HL(HL + 1); cyc = 8; break;
    case 0x33: CPU.sp++; cyc = 8; break;
    case 0x0B: SET_BC(BC - 1); cyc = 8; break;
    case 0x1B: SET_DE(DE - 1); cyc = 8; break;
    case 0x2B: SET_HL(HL - 1); cyc = 8; break;
    case 0x3B: CPU.sp--; cyc = 8; break;
    case 0x09: SET_HL(alu_add16(HL, BC, &CPU.f)); cyc = 8; break;
    case 0x19: SET_HL(alu_add16(HL, DE, &CPU.f)); cyc = 8; break;
    case 0x29: SET_HL(alu_add16(HL, HL, &CPU.f)); cyc = 8; break;
    case 0x39: SET_HL(alu_add16(HL, CPU.sp, &CPU.f)); cyc = 8; break;
    case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C: {
        int r;
        r = (op >> 3) & 7;
        set_r(r, alu_inc(get_r(r), &CPU.f));
        cyc = r == 6 ? 12 : 4;
        break;
    }
    case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D: {
        int r;
        r = (op >> 3) & 7;
        set_r(r, alu_dec(get_r(r), &CPU.f));
        cyc = r == 6 ? 12 : 4;
        break;
    }
    case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E: {
        int r;
        r = (op >> 3) & 7;
        set_r(r, fetch8());
        cyc = r == 6 ? 12 : 8;
        break;
    }
    case 0x07: CPU.a = alu_rlca(CPU.a, &CPU.f); break;
    case 0x0F: CPU.a = alu_rrca(CPU.a, &CPU.f); break;
    case 0x17: CPU.a = alu_rla(CPU.a, &CPU.f); break;
    case 0x1F: CPU.a = alu_rra(CPU.a, &CPU.f); break;
    case 0x08: {
        U16 a;
        a = fetch16();
        mem_wr(a, (U8)CPU.sp);
        mem_wr((U16)(a + 1), (U8)(CPU.sp >> 8));
        cyc = 20;
        break;
    }
    case 0x10: (void)fetch8(); break; /* STOP */
    case 0x18: { S8 e; e = (S8)fetch8(); CPU.pc = (U16)(CPU.pc + e); cyc = 12; break; }
    case 0x20: case 0x28: case 0x30: case 0x38: {
        S8 e;
        e = (S8)fetch8();
        cyc = 8;
        if (cond((op >> 3) & 3)) { CPU.pc = (U16)(CPU.pc + e); cyc = 12; }
        break;
    }
    case 0x27: CPU.a = alu_daa(CPU.a, &CPU.f); break;
    case 0x2F: CPU.a = (U8)~CPU.a; CPU.f |= FLAG_N | FLAG_H; break;
    case 0x37: CPU.f = (U8)((CPU.f & FLAG_Z) | FLAG_C); break;
    case 0x3F: CPU.f = (U8)((CPU.f & FLAG_Z) | ((CPU.f & FLAG_C) ^ FLAG_C)); break;

    case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        cyc = 8;
        if (cond((op >> 3) & 3)) { CPU.pc = pop16(); cyc = 20; }
        break;
    case 0xC9: CPU.pc = pop16(); cyc = 16; break;
    case 0xD9: CPU.pc = pop16(); CPU.ime = 1; cyc = 16; gb_update_deadline(); break;
    case 0xC1: SET_BC(pop16()); cyc = 12; break;
    case 0xD1: SET_DE(pop16()); cyc = 12; break;
    case 0xE1: SET_HL(pop16()); cyc = 12; break;
    case 0xF1: { U16 v; v = pop16(); CPU.a = (U8)(v >> 8); CPU.f = (U8)(v & 0xF0); cyc = 12; break; }
    case 0xC5: push16(BC); cyc = 16; break;
    case 0xD5: push16(DE); cyc = 16; break;
    case 0xE5: push16(HL); cyc = 16; break;
    case 0xF5: push16((U16)((CPU.a << 8) | CPU.f)); cyc = 16; break;
    case 0xC2: case 0xCA: case 0xD2: case 0xDA: {
        U16 a;
        a = fetch16();
        cyc = 12;
        if (cond((op >> 3) & 3)) { CPU.pc = a; cyc = 16; }
        break;
    }
    case 0xC3: CPU.pc = fetch16(); cyc = 16; break;
    case 0xE9: CPU.pc = HL; break;
    case 0xC4: case 0xCC: case 0xD4: case 0xDC: {
        U16 a;
        a = fetch16();
        cyc = 12;
        if (cond((op >> 3) & 3)) { push16(CPU.pc); CPU.pc = a; cyc = 24; }
        break;
    }
    case 0xCD: { U16 a; a = fetch16(); push16(CPU.pc); CPU.pc = a; cyc = 24; break; }
    case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF:
        push16(CPU.pc);
        CPU.pc = (U16)(op & 0x38);
        cyc = 16;
        break;
    case 0xC6: alu_op(0, fetch8()); cyc = 8; break;
    case 0xCE: alu_op(1, fetch8()); cyc = 8; break;
    case 0xD6: alu_op(2, fetch8()); cyc = 8; break;
    case 0xDE: alu_op(3, fetch8()); cyc = 8; break;
    case 0xE6: alu_op(4, fetch8()); cyc = 8; break;
    case 0xEE: alu_op(5, fetch8()); cyc = 8; break;
    case 0xF6: alu_op(6, fetch8()); cyc = 8; break;
    case 0xFE: alu_op(7, fetch8()); cyc = 8; break;
    case 0xCB: cyc = exec_cb(); break;
    case 0xE0: mem_wrh(fetch8(), CPU.a); cyc = 12; break;
    case 0xF0: CPU.a = mem_rdh(fetch8()); cyc = 12; break;
    case 0xE2: mem_wrh(CPU.c, CPU.a); cyc = 8; break;
    case 0xF2: CPU.a = mem_rdh(CPU.c); cyc = 8; break;
    case 0xEA: mem_wr(fetch16(), CPU.a); cyc = 16; break;
    case 0xFA: CPU.a = mem_rd(fetch16()); cyc = 16; break;
    case 0xE8: CPU.sp = alu_addsp(CPU.sp, (S8)fetch8(), &CPU.f); cyc = 16; break;
    case 0xF8: SET_HL(alu_addsp(CPU.sp, (S8)fetch8(), &CPU.f)); cyc = 12; break;
    case 0xF9: CPU.sp = HL; cyc = 8; break;
    case 0xF3: CPU.ime = 0; CPU.ime_delay = 0; break;
    case 0xFB: CPU.ime_delay = 1; break;
    default:
        gb_log("Ungueltiger Opcode %02X bei %02X:%04X\n", op, g_gb.rom_bank, (U16)(CPU.pc - 1));
        CPU.halted = 1;
        break;
    }
    g_gb.cycles += (U32)cyc;
    return cyc;
}
