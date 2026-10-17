/*
 * gb_sgb.c - Super-Game-Boy-Befehle (Paketempfang ueber P1).
 *
 * Unterstuetzt: PAL01/23/03/12, ATTR_BLK, ATTR_LIN, ATTR_DIV, ATTR_CHR,
 * PAL_SET, PAL_TRN, ATTR_TRN, ATTR_SET, MLT_REQ, CHR_TRN, PCT_TRN, MASK_EN.
 * VRAM-Transfers werden aus dem naechsten gerenderten Frame gelesen,
 * genau wie der echte SGB das sichtbare Bild abgreift.
 */
#include <string.h>
#include "gb.h"

#define SGB (g_gb.sgb)

enum {
    CMD_PAL01 = 0x00, CMD_PAL23, CMD_PAL03, CMD_PAL12,
    CMD_ATTR_BLK, CMD_ATTR_LIN, CMD_ATTR_DIV, CMD_ATTR_CHR,
    CMD_SOUND, CMD_SOU_TRN, CMD_PAL_SET, CMD_PAL_TRN,
    CMD_ATRC_EN, CMD_TEST_EN, CMD_ICON_EN, CMD_DATA_SND,
    CMD_DATA_TRN, CMD_MLT_REQ, CMD_JUMP, CMD_CHR_TRN,
    CMD_PCT_TRN, CMD_ATTR_TRN, CMD_ATTR_SET, CMD_MASK_EN,
    CMD_OBJ_TRN
};

static U16 rd16(const U8 *p)
{
    return (U16)(p[0] | (p[1] << 8));
}

void gb_sgb_reset(void)
{
    int p;
    static const U16 def[4] = { 0x67BF, 0x265B, 0x10B5, 0x2866 };
    memset(&SGB, 0, sizeof(SGB));
    for (p = 0; p < 4; p++) {
        memcpy(SGB.pal[p], def, sizeof(def));
    }
    SGB.last_p1 = 0x30;
}

static void set_pal_pair(int a, int b, const U8 *d)
{
    U16 c0;
    c0 = rd16(d + 1);
    SGB.pal[0][0] = c0;
    SGB.pal[1][0] = c0;
    SGB.pal[2][0] = c0;
    SGB.pal[3][0] = c0;
    SGB.pal[a][1] = rd16(d + 3);
    SGB.pal[a][2] = rd16(d + 5);
    SGB.pal[a][3] = rd16(d + 7);
    SGB.pal[b][1] = rd16(d + 9);
    SGB.pal[b][2] = rd16(d + 11);
    SGB.pal[b][3] = rd16(d + 13);
}

static void fill_rect(int x1, int y1, int x2, int y2, int pal)
{
    int x, y;
    for (y = y1; y <= y2 && y < 18; y++) {
        for (x = x1; x <= x2 && x < 20; x++) {
            if (x >= 0 && y >= 0) {
                SGB.attr[y][x] = (U8)pal;
            }
        }
    }
}

static void attr_blk(const U8 *d)
{
    int n, i;
    n = d[1];
    for (i = 0; i < n && i < 18; i++) {
        const U8 *s;
        int ctrl, pin, pbd, pout, x1, y1, x2, y2, x, y;
        s = d + 2 + i * 6;
        ctrl = s[0] & 7;
        pin = s[1] & 3;
        pbd = (s[1] >> 2) & 3;
        pout = (s[1] >> 4) & 3;
        x1 = s[2]; y1 = s[3]; x2 = s[4]; y2 = s[5];
        /* nur Rand angegeben -> Innen = Rand und umgekehrt */
        if (ctrl == 1) pbd = pin;
        if (ctrl == 4) pbd = pout;
        for (y = 0; y < 18; y++) {
            for (x = 0; x < 20; x++) {
                int inside, border;
                inside = x > x1 && x < x2 && y > y1 && y < y2;
                border = (x >= x1 && x <= x2 && y >= y1 && y <= y2) && !inside;
                if (inside && (ctrl & 1)) SGB.attr[y][x] = (U8)pin;
                else if (border && (ctrl & 3)) SGB.attr[y][x] = (U8)pbd;
                else if (!inside && !border && (ctrl & 4)) SGB.attr[y][x] = (U8)pout;
            }
        }
    }
}

static void attr_lin(const U8 *d)
{
    int n, i;
    n = d[1];
    for (i = 0; i < n && i < 110; i++) {
        U8 v;
        int line, pal;
        v = d[2 + i];
        line = v & 0x1F;
        pal = (v >> 5) & 3;
        if (v & 0x80) fill_rect(0, line, 19, line, pal);
        else fill_rect(line, 0, line, 17, pal);
    }
}

static void attr_div(const U8 *d)
{
    int pb, pa, pl, pos;
    pb = d[1] & 3;
    pa = (d[1] >> 2) & 3;
    pl = (d[1] >> 4) & 3;
    pos = d[2];
    if (d[1] & 0x40) {
        fill_rect(0, 0, 19, pos - 1, pa);
        fill_rect(0, pos, 19, pos, pl);
        fill_rect(0, pos + 1, 19, 17, pb);
    } else {
        fill_rect(0, 0, pos - 1, 17, pa);
        fill_rect(pos, 0, pos, 17, pl);
        fill_rect(pos + 1, 0, 19, 17, pb);
    }
}

static void attr_chr(const U8 *d, int len)
{
    int x, y, n, dir, i;
    x = d[1]; y = d[2];
    n = rd16(d + 3);
    dir = d[5];
    for (i = 0; i < n && 6 + i / 4 < len; i++) {
        int pal;
        pal = (d[6 + i / 4] >> (6 - (i & 3) * 2)) & 3;
        if (x < 20 && y < 18) SGB.attr[y][x] = (U8)pal;
        if (dir == 0) { x++; if (x >= 20) { x = 0; y++; } }
        else { y++; if (y >= 18) { y = 0; x++; } }
    }
}

static void apply_attr_file(int f)
{
    int i;
    if (f >= 45) return;
    for (i = 0; i < 360; i++) {
        SGB.attr[i / 20][i % 20] = (U8)((SGB.attr_files[f][i / 4] >> (6 - (i & 3) * 2)) & 3);
    }
}

static void process_command(void)
{
    const U8 *d;
    int cmd, len;
    d = SGB.bytes;
    cmd = d[0] >> 3;
    len = (d[0] & 7) * 16;
    switch (cmd) {
    case CMD_PAL01: set_pal_pair(0, 1, d); break;
    case CMD_PAL23: set_pal_pair(2, 3, d); break;
    case CMD_PAL03: set_pal_pair(0, 3, d); break;
    case CMD_PAL12: set_pal_pair(1, 2, d); break;
    case CMD_ATTR_BLK: attr_blk(d); break;
    case CMD_ATTR_LIN: attr_lin(d); break;
    case CMD_ATTR_DIV: attr_div(d); break;
    case CMD_ATTR_CHR: attr_chr(d, len); break;
    case CMD_PAL_SET: {
        int p, c;
        for (p = 0; p < 4; p++) {
            U16 idx;
            idx = (U16)(rd16(d + 1 + p * 2) & 0x1FF);
            for (c = 0; c < 4; c++) {
                SGB.pal[p][c] = SGB.sys_pal[idx][c];
            }
        }
        /* Farbe 0 ist bei allen Paletten gemeinsam */
        for (p = 1; p < 4; p++) {
            SGB.pal[p][0] = SGB.pal[0][0];
        }
        if (d[9] & 0x80) {
            apply_attr_file(d[9] & 0x3F);
        }
        if (d[9] & 0x40) {
            SGB.mask = 0;
        }
        break;
    }
    case CMD_PAL_TRN:
    case CMD_ATTR_TRN:
    case CMD_PCT_TRN:
        SGB.pending_trn = (U8)cmd;
        SGB.pending_arg = 0;
        break;
    case CMD_CHR_TRN:
        SGB.pending_trn = (U8)cmd;
        SGB.pending_arg = (U8)(d[1] & 1);
        break;
    case CMD_ATTR_SET:
        apply_attr_file(d[1] & 0x3F);
        if (d[1] & 0x40) SGB.mask = 0;
        break;
    case CMD_MLT_REQ:
        SGB.mlt = (U8)(d[1] & 3);
        SGB.joy_id = 0;
        break;
    case CMD_MASK_EN:
        SGB.mask = (U8)(d[1] & 3);
        break;
    default:
        break;
    }
}

void gb_sgb_p1_write(U8 val)
{
    U8 sel, prev;
    sel = (U8)(val & 0x30);
    prev = SGB.last_p1;
    SGB.last_p1 = sel;

    if (sel == 0x00) {
        /* Reset-Puls: neues Paket */
        SGB.receiving = 1;
        SGB.bitpos = 0;
        memset(SGB.packet, 0, sizeof(SGB.packet));
        return;
    }
    if (sel == 0x30) {
        if (SGB.mlt && !(prev & 0x10) && !SGB.receiving) {
            SGB.joy_id = (U8)((SGB.joy_id + 1) % (SGB.mlt + 1));
        }
        return;
    }
    if (!SGB.receiving || prev != 0x30) {
        return;
    }
    /* sel == 0x20 -> Bit 0, sel == 0x10 -> Bit 1 */
    if (SGB.bitpos < 128) {
        if (sel == 0x10) {
            SGB.packet[SGB.bitpos >> 3] |= (U8)(1 << (SGB.bitpos & 7));
        }
        SGB.bitpos++;
        return;
    }
    /* Stopbit */
    SGB.receiving = 0;
    if (SGB.cur_packet == 0) {
        SGB.npackets = (U8)(SGB.packet[0] & 7);
        if (SGB.npackets == 0) SGB.npackets = 1;
    }
    memcpy(&SGB.bytes[SGB.cur_packet * 16], SGB.packet, 16);
    SGB.cur_packet++;
    if (SGB.cur_packet >= SGB.npackets) {
        SGB.cur_packet = 0;
        process_command();
    }
}

/* Liest die 4 KiB, die der SGB aus dem sichtbaren Bild abgreift. */
static void capture_vram(U8 *out)
{
    int t, row, x;
    for (t = 0; t < 256; t++) {
        int tx, ty;
        tx = t % 20;
        ty = t / 20;
        for (row = 0; row < 8; row++) {
            U8 lo, hi;
            const U8 *sp;
            lo = 0;
            hi = 0;
            sp = &g_gb.shade[(ty * 8 + row) * GB_W + tx * 8];
            for (x = 0; x < 8; x++) {
                lo |= (U8)((sp[x] & 1) << (7 - x));
                hi |= (U8)(((sp[x] >> 1) & 1) << (7 - x));
            }
            out[t * 16 + row * 2] = lo;
            out[t * 16 + row * 2 + 1] = hi;
        }
    }
}

void gb_sgb_frame(void)
{
    static U8 buf[4096];
    int i;
    U8 cmd;

    cmd = SGB.pending_trn;
    if (!cmd) {
        return;
    }
    if (!(g_gb.io[0x40] & 0x80)) {
        return;   /* warten bis das LCD wieder an ist */
    }
    SGB.pending_trn = 0;
    capture_vram(buf);
    switch (cmd) {
    case CMD_PAL_TRN:
        for (i = 0; i < 512; i++) {
            int c;
            for (c = 0; c < 4; c++) {
                SGB.sys_pal[i][c] = rd16(&buf[i * 8 + c * 2]);
            }
        }
        break;
    case CMD_ATTR_TRN:
        memcpy(SGB.attr_files, buf, sizeof(SGB.attr_files));
        break;
    case CMD_CHR_TRN:
        memcpy(&SGB.border_tiles[SGB.pending_arg * 4096], buf, 4096);
        break;
    case CMD_PCT_TRN:
        for (i = 0; i < 32 * 32; i++) {
            SGB.border_map[i] = rd16(&buf[i * 2]);
        }
        for (i = 0; i < 64; i++) {
            SGB.border_pal[i] = rd16(&buf[0x800 + i * 2]);
        }
        SGB.border_valid = 1;
        break;
    default:
        break;
    }
}
