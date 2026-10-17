/*
 * gb_core.c - Speicher, I/O, MBC5, Timer, Serial, Joypad, Hauptschleife.
 */
#include <string.h>
#include "gb.h"
#include "gb_mem.h"

GB g_gb;

static int s_use_recomp = 1;
void (*gb_miss_hook)(U16 bank, U16 pc) = NULL;
static U32 s_sysclk;          /* interner 16-Bit-Zaehler (DIV) als 32 Bit */

static const U16 s_tac_period[4] = { 1024, 16, 64, 256 };

/* ------------------------------------------------------------------ */
/* Initialisierung                                                     */
/* ------------------------------------------------------------------ */

void gb_init(const U8 *rom, U32 rom_size)
{
    memset(&g_gb, 0, sizeof(g_gb));
    g_gb.rom = rom;
    g_gb.rom_size = rom_size;
    g_gb.color_mode = COLOR_SGB;
    memset(g_gb.sram, 0xFF, sizeof(g_gb.sram));
    if (recomp_available) {
        recomp_register_all();
    }
    gb_reset();
}

void gb_reset(void)
{
    U8 sram_copy_flag;

    sram_copy_flag = g_gb.sram_dirty;
    g_gb.cpu.a = 0x01;
    g_gb.cpu.f = 0x00;
    g_gb.cpu.b = 0x00;
    g_gb.cpu.c = 0x14;
    g_gb.cpu.d = 0x00;
    g_gb.cpu.e = 0x00;
    g_gb.cpu.h = 0xC0;
    g_gb.cpu.l = 0x60;
    g_gb.cpu.sp = 0xFFFE;
    g_gb.cpu.pc = 0x0100;
    g_gb.cpu.ime = 0;
    g_gb.cpu.ime_delay = 0;
    g_gb.cpu.halted = 0;

    g_gb.rom_bank = 1;
    g_gb.ram_bank = 0;
    g_gb.ram_enable = 0;
    memset(g_gb.vram, 0, sizeof(g_gb.vram));
    memset(g_gb.wram, 0, sizeof(g_gb.wram));
    memset(g_gb.oam, 0, sizeof(g_gb.oam));
    memset(g_gb.hram, 0, sizeof(g_gb.hram));
    memset(g_gb.io, 0, sizeof(g_gb.io));
    g_gb.ie = 0;
    g_gb.io[0x00] = 0xCF;
    g_gb.io[0x40] = 0x91;
    g_gb.io[0x47] = 0xFC;
    g_gb.io[0x48] = 0xFF;
    g_gb.io[0x49] = 0xFF;
    g_gb.io[0x0F] = 0xE1;

    g_gb.cycles = 0;
    g_gb.timer_clock = 0;
    g_gb.serial_active = 0;
    s_sysclk = 0xABCC;
    g_gb.sram_dirty = sram_copy_flag;

    gb_ppu_reset();
    gb_apu_reset();
    gb_sgb_reset();
    gb_update_deadline();
}

void gb_set_recompiled(int enabled)
{
    s_use_recomp = enabled && recomp_available;
}

void gb_set_buttons(U8 buttons)
{
    U8 old;
    old = g_gb.buttons;
    g_gb.buttons = buttons;
    if ((buttons & ~old) != 0) {
        g_gb.io[0x0F] |= INT_JOYPAD;
    }
}

void gb_set_color_mode(U8 mode)
{
    g_gb.color_mode = mode;
}

U8 gb_rom_read_banked(U8 bank, U16 addr)
{
    if (addr < 0x4000) {
        return g_gb.rom[addr];
    }
    return g_gb.rom[((U32)bank << 14) | (addr & 0x3FFF)];
}

U8 gb_read(U16 addr)
{
    return mem_rd(addr);
}

void gb_write(U16 addr, U8 val)
{
    mem_wr(addr, val);
}

U16 gb_pop16(U16 sp)
{
    return (U16)(mem_rd(sp) | (mem_rd((U16)(sp + 1)) << 8));
}

void gb_push16(U16 sp, U16 v)
{
    mem_wr(sp, (U8)(v & 0xFF));
    mem_wr((U16)(sp + 1), (U8)(v >> 8));
}

/* ------------------------------------------------------------------ */
/* MBC5                                                                */
/* ------------------------------------------------------------------ */

void gb_mbc_write(U16 addr, U8 val)
{
    U32 nbanks;

    nbanks = g_gb.rom_size >> 14;
    if (addr < 0x2000) {
        U8 was;
        was = g_gb.ram_enable;
        g_gb.ram_enable = (U8)((val & 0x0F) == 0x0A);
        if (was && !g_gb.ram_enable) {
            /* Spiel hat SRAM geschlossen -> guter Zeitpunkt zum Sichern */
            g_gb.sram_dirty |= 2;
        }
    } else if (addr < 0x3000) {
        g_gb.rom_bank = (U16)((g_gb.rom_bank & 0x100) | val);
    } else if (addr < 0x4000) {
        g_gb.rom_bank = (U16)((g_gb.rom_bank & 0xFF) | ((val & 1) << 8));
    } else if (addr < 0x6000) {
        g_gb.ram_bank = (U8)(val & 0x0F);
    }
    if (nbanks) {
        g_gb.rom_bank = (U16)(g_gb.rom_bank % nbanks);
    }
}

/* ------------------------------------------------------------------ */
/* Timer                                                               */
/* ------------------------------------------------------------------ */

void gb_timer_sync(void)
{
    U32 elapsed, period, before, after, incs;
    U8 tac;

    elapsed = g_gb.cycles - g_gb.timer_clock;
    if (elapsed == 0) {
        return;
    }
    g_gb.timer_clock = g_gb.cycles;
    tac = g_gb.io[0x07];
    if (tac & 4) {
        period = s_tac_period[tac & 3];
        before = s_sysclk / period;
        after = (s_sysclk + elapsed) / period;
        incs = after - before;
        while (incs > 0) {
            U32 room;
            room = 0x100 - g_gb.io[0x05];
            if (incs >= room) {
                incs -= room;
                g_gb.io[0x05] = g_gb.io[0x06];
                g_gb.io[0x0F] |= INT_TIMER;
            } else {
                g_gb.io[0x05] = (U8)(g_gb.io[0x05] + incs);
                incs = 0;
            }
        }
    }
    s_sysclk += elapsed;
}

static U32 timer_next_event(void)
{
    U32 period, until;
    U8 tac;

    tac = g_gb.io[0x07];
    if (!(tac & 4)) {
        return 0xFFFFFFFFu;
    }
    period = s_tac_period[tac & 3];
    until = (U32)(0x100 - g_gb.io[0x05]) * period - (s_sysclk % period);
    return g_gb.timer_clock + until;
}

/* ------------------------------------------------------------------ */
/* Serial (ohne Partner: Master-Transfers liefern 0xFF)                */
/* ------------------------------------------------------------------ */

static void serial_sync(void)
{
    if (g_gb.serial_active && (S32)(g_gb.cycles - g_gb.serial_end) >= 0) {
        g_gb.serial_active = 0;
        g_gb.io[0x01] = 0xFF;
        g_gb.io[0x02] &= 0x7F;
        g_gb.io[0x0F] |= INT_SERIAL;
    }
}

/* ------------------------------------------------------------------ */
/* Synchronisation                                                     */
/* ------------------------------------------------------------------ */

void gb_update_deadline(void)
{
    U32 d, t;
    extern U32 gb_ppu_next_event(void);

    if (g_gb.cpu.ime && (g_gb.ie & g_gb.io[0x0F] & 0x1F)) {
        g_gb.deadline = g_gb.cycles;
        return;
    }
    d = gb_ppu_next_event();
    t = timer_next_event();
    if ((S32)(t - d) < 0) {
        d = t;
    }
    if (g_gb.serial_active && (S32)(g_gb.serial_end - d) < 0) {
        d = g_gb.serial_end;
    }
    g_gb.deadline = d;
}

void gb_sync(void)
{
    gb_ppu_sync();
    gb_timer_sync();
    serial_sync();
    gb_update_deadline();
}

/* ------------------------------------------------------------------ */
/* I/O-Register                                                        */
/* ------------------------------------------------------------------ */

U8 gb_io_read(U16 addr)
{
    U8 reg;

    reg = (U8)(addr & 0x7F);
    gb_sync();
    switch (reg) {
    case 0x00: {
        U8 p1, v;
        p1 = g_gb.io[0x00];
        v = 0x0F;
        if (g_gb.sgb.mlt && (p1 & 0x30) == 0x30) {
            return (U8)(0xC0 | p1 | (0x0F - g_gb.sgb.joy_id));
        }
        if (!(p1 & 0x10)) {
            v &= (U8)~((g_gb.buttons >> 4) & 0x0F);
        }
        if (!(p1 & 0x20)) {
            v &= (U8)~(g_gb.buttons & 0x0F);
        }
        return (U8)(0xC0 | (p1 & 0x30) | v);
    }
    case 0x04:
        return (U8)(s_sysclk >> 8);
    case 0x0F:
        return (U8)(0xE0 | g_gb.io[0x0F]);
    default:
        if (reg >= 0x10 && reg < 0x40) {
            return gb_apu_read(addr);
        }
        if (reg >= 0x40 && reg < 0x4C) {
            if (reg == 0x41) {
                return (U8)(0x80 | g_gb.io[0x41]);
            }
            return g_gb.io[reg];
        }
        return g_gb.io[reg];
    }
}

void gb_io_write(U16 addr, U8 val)
{
    U8 reg;

    reg = (U8)(addr & 0x7F);
    gb_sync();
    switch (reg) {
    case 0x00:
        g_gb.io[0x00] = (U8)(val & 0x30);
        gb_sgb_p1_write(val);
        break;
    case 0x01:
        g_gb.io[0x01] = val;
        break;
    case 0x02:
        g_gb.io[0x02] = val;
        if ((val & 0x81) == 0x81) {
            g_gb.serial_active = 1;
            g_gb.serial_end = g_gb.cycles + 8 * 512;
        } else {
            g_gb.serial_active = 0;
        }
        break;
    case 0x04:
        s_sysclk = 0;
        break;
    case 0x05:
    case 0x06:
    case 0x07:
        g_gb.io[reg] = val;
        break;
    case 0x0F:
        g_gb.io[0x0F] = (U8)(val & 0x1F);
        break;
    case 0x46: {
        U16 src;
        int i;
        src = (U16)(val << 8);
        g_gb.io[0x46] = val;
        for (i = 0; i < 0xA0; i++) {
            g_gb.oam[i] = mem_rd((U16)(src + i));
        }
        break;
    }
    default:
        if (reg >= 0x10 && reg < 0x40) {
            gb_apu_write(addr, val);
        } else if (reg >= 0x40 && reg < 0x4C) {
            gb_ppu_write(addr, val);
        } else {
            g_gb.io[reg] = val;
        }
        break;
    }
    gb_update_deadline();
}

/* ------------------------------------------------------------------ */
/* Interrupts                                                          */
/* ------------------------------------------------------------------ */

void gb_service_interrupts(void)
{
    U8 pending;
    int i;

    pending = (U8)(g_gb.ie & g_gb.io[0x0F] & 0x1F);
    if (!pending) {
        return;
    }
    g_gb.cpu.halted = 0;
    if (!g_gb.cpu.ime) {
        return;
    }
    for (i = 0; i < 5; i++) {
        if (pending & (1 << i)) {
            break;
        }
    }
    g_gb.io[0x0F] &= (U8)~(1 << i);
    g_gb.cpu.ime = 0;
    g_gb.cpu.sp = (U16)(g_gb.cpu.sp - 2);
    mem_wr(g_gb.cpu.sp, (U8)(g_gb.cpu.pc & 0xFF));
    mem_wr((U16)(g_gb.cpu.sp + 1), (U8)(g_gb.cpu.pc >> 8));
    g_gb.cpu.pc = (U16)(0x40 + i * 8);
    g_gb.cycles += 20;
    gb_update_deadline();
}

/* ------------------------------------------------------------------ */
/* Hauptschleife                                                       */
/* ------------------------------------------------------------------ */

static void rebase_clocks(void)
{
    U32 delta;

    delta = g_gb.cycles;
    g_gb.cycles -= delta;
    g_gb.ppu_clock -= delta;
    g_gb.timer_clock -= delta;
    g_gb.serial_end -= delta;
    gb_apu_rebase(delta);
    gb_update_deadline();
}

void gb_run_frame(void)
{
    GBBlockFn fn;

    g_gb.frame_done = 0;
    while (!g_gb.frame_done) {
        if ((S32)(g_gb.cycles - g_gb.deadline) >= 0) {
            gb_sync();
            gb_service_interrupts();
        }
        if (g_gb.cpu.halted) {
            /* bis zum naechsten Ereignis vorspulen */
            if ((S32)(g_gb.deadline - g_gb.cycles) > 0) {
                g_gb.cycles = g_gb.deadline;
            } else {
                g_gb.cycles += 4;
            }
            gb_sync();
            gb_service_interrupts();
            continue;
        }
        if (g_gb.cpu.ime_delay) {
            g_gb.cpu.ime_delay = 0;
            g_gb.cpu.ime = 1;
            gb_update_deadline();
            (void)gb_interp_step();
            continue;
        }
        fn = s_use_recomp ? recomp_lookup(g_gb.cpu.pc) : NULL;
        if (fn) {
            g_gb.dispatch_count++;
            fn();
        } else {
            if (gb_miss_hook && s_use_recomp && g_gb.cpu.pc < 0x8000) {
                gb_miss_hook(g_gb.cpu.pc < 0x4000 ? 0 : g_gb.rom_bank, g_gb.cpu.pc);
            }
            (void)gb_interp_step();
        }
    }
    gb_apu_sync();
    gb_sgb_frame();
    gb_ppu_compose_frame();
    rebase_clocks();
}
