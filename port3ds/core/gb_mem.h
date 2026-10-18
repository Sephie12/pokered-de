/*
 * gb_mem.h - schnelle Speicherzugriffe (inline) fuer Interpreter und
 * rekompilierten Code. I/O-Register gehen ueber den langsamen Pfad,
 * der vorher PPU/Timer auf den aktuellen Zyklus synchronisiert.
 */
#ifndef GB_MEM_H
#define GB_MEM_H

#include "gb.h"

U8   gb_io_read(U16 addr);
void gb_io_write(U16 addr, U8 val);
void gb_mbc_write(U16 addr, U8 val);

GB_INLINE U8 mem_rd(U16 addr)
{
    if (addr < 0x4000) {
        return g_gb.rom[addr];
    }
    if (addr < 0x8000) {
        return g_gb.rom[((U32)g_gb.rom_bank << 14) | (addr & 0x3FFF)];
    }
    if (addr < 0xA000) {
        return g_gb.vram[addr & 0x1FFF];
    }
    if (addr < 0xC000) {
        if (!g_gb.ram_enable) {
            return 0xFF;
        }
        return g_gb.sram[((U32)(g_gb.ram_bank & 3) << 13) | (addr & 0x1FFF)];
    }
    if (addr < 0xFE00) {
        return g_gb.wram[addr & 0x1FFF];
    }
    if (addr >= 0xFF80) {
        if (addr == 0xFFFF) {
            return g_gb.ie;
        }
        return g_gb.hram[addr & 0x7F];
    }
    if (addr < 0xFEA0) {
        return g_gb.oam[addr - 0xFE00];
    }
    if (addr < 0xFF00) {
        return 0xFF;
    }
    return gb_io_read(addr);
}

GB_INLINE void mem_wr(U16 addr, U8 val)
{
    if (addr >= 0xC000) {
        if (addr < 0xFE00) {
            g_gb.wram[addr & 0x1FFF] = val;
            return;
        }
        if (addr >= 0xFF80) {
            if (addr == 0xFFFF) {
                g_gb.ie = val;
                gb_update_deadline();
                return;
            }
            g_gb.hram[addr & 0x7F] = val;
            return;
        }
        if (addr < 0xFEA0) {
            g_gb.oam[addr - 0xFE00] = val;
            return;
        }
        if (addr < 0xFF00) {
            return;
        }
        gb_io_write(addr, val);
        return;
    }
    if (addr < 0x8000) {
        gb_mbc_write(addr, val);
        return;
    }
    if (addr < 0xA000) {
        g_gb.vram[addr & 0x1FFF] = val;
        return;
    }
    if (g_gb.ram_enable) {
        g_gb.sram[((U32)(g_gb.ram_bank & 3) << 13) | (addr & 0x1FFF)] = val;
        g_gb.sram_dirty |= 1;
    }
}

/* Spezialisierte Varianten fuer ldh (0xFF00 + n) */
GB_INLINE U8 mem_rdh(U8 n)
{
    if (n >= 0x80) {
        if (n == 0xFF) {
            return g_gb.ie;
        }
        return g_gb.hram[n & 0x7F];
    }
    return gb_io_read((U16)(0xFF00 | n));
}

GB_INLINE void mem_wrh(U8 n, U8 val)
{
    if (n >= 0x80) {
        if (n == 0xFF) {
            g_gb.ie = val;
            gb_update_deadline();
            return;
        }
        g_gb.hram[n & 0x7F] = val;
        return;
    }
    gb_io_write((U16)(0xFF00 | n), val);
}

#endif
