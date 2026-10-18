/*
 * gb_ppu.c - Scanline-Renderer und PPU-Timing.
 *
 * Jede Zeile wird beim Uebergang Mode 3 -> Mode 0 mit den aktuellen
 * Registern gerendert. Gespeichert wird pro Pixel der Graustufenwert
 * nach BGP/OBP und die Ebene (BG/OBJ0/OBJ1); daraus setzt
 * gb_ppu_compose_frame() je nach Farbmodus das RGB565-Bild zusammen.
 */
#include <string.h>
#include "gb.h"

#define LINE_CYCLES 456
#define MODE2_END   80
#define MODE3_END   252

/* RGB565 aus 5-Bit-Komponenten */
#define RGB565(r, g, b) ((U16)(((r) << 11) | ((g) << 6) | (b)))
/* RGB888 -> RGB565 */
#define C24(x) ((U16)((((x) >> 19) & 0x1F) << 11 | (((x) >> 10) & 0x3F) << 5 | (((x) >> 3) & 0x1F)))

static U16 s_dmg_pal[4];
static U16 s_cgb_bg[4];
static U16 s_cgb_obj0[4];
static U16 s_cgb_obj1[4];

/* Klassische DMG-Farben */
static const U32 k_dmg[4] = { 0xC4CFA1, 0x8B956D, 0x4D533C, 0x1F1F1F };
/* GBC-Boot-ROM-Kompatibilitaetspalette fuer "POKEMON RED" */
static const U32 k_cgb_bg[4]   = { 0xFFFFFF, 0xFF8484, 0x943A3A, 0x000000 };
static const U32 k_cgb_obj0[4] = { 0xFFFFFF, 0x7BFF31, 0x008400, 0x000000 };
static const U32 k_cgb_obj1[4] = { 0xFFFFFF, 0xFF8484, 0x943A3A, 0x000000 };

static U16 s_prev_frame[GB_W * GB_H];

U16 gb_bgr15_to_rgb565(U16 c)
{
    U16 r, g, b;
    r = (U16)(c & 0x1F);
    g = (U16)((c >> 5) & 0x1F);
    b = (U16)((c >> 10) & 0x1F);
    return (U16)((r << 11) | (g << 6) | ((g >> 4) << 5) | b);
}

void gb_ppu_reset(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        s_dmg_pal[i] = C24(k_dmg[i]);
        s_cgb_bg[i] = C24(k_cgb_bg[i]);
        s_cgb_obj0[i] = C24(k_cgb_obj0[i]);
        s_cgb_obj1[i] = C24(k_cgb_obj1[i]);
    }
    g_gb.ppu_clock = 0;
    g_gb.ppu_line_cycle = 0;
    g_gb.ppu_mode = 2;
    g_gb.window_line = 0;
    g_gb.io[0x44] = 0;
    memset(g_gb.shade, 0, sizeof(g_gb.shade));
    memset(g_gb.layer, 0, sizeof(g_gb.layer));
    memset(s_prev_frame, 0xFF, sizeof(s_prev_frame));
}

/* ------------------------------------------------------------------ */
/* Zeile rendern                                                       */
/* ------------------------------------------------------------------ */

static void render_line(int ly)
{
    U8 lcdc, scx, scy, wx, wy, bgp, obp0, obp1;
    U8 bgcol[GB_W];
    U8 *shade;
    U8 *layer;
    int x, i, nspr, win_drawn;
    int spr_idx[10];
    int sprh;

    lcdc = g_gb.io[0x40];
    scy = g_gb.io[0x42];
    scx = g_gb.io[0x43];
    bgp = g_gb.io[0x47];
    obp0 = g_gb.io[0x48];
    obp1 = g_gb.io[0x49];
    wy = g_gb.io[0x4A];
    wx = g_gb.io[0x4B];
    if (g_gb.bg_override_on) {
        /* Das Spiel zeigt sein Bild oft ueber das Fenster: Override ersetzt alles */
        scx = 0;
        scy = 0;
        lcdc = (U8)(lcdc | 0x01);
    }
    shade = &g_gb.shade[ly * GB_W];
    layer = &g_gb.layer[ly * GB_W];
    win_drawn = 0;

    /* Hintergrund */
    if (lcdc & 0x01) {
        U16 map_base;
        int y, tile_row;
        map_base = (lcdc & 0x08) ? 0x1C00 : 0x1800;
        y = (ly + scy) & 0xFF;
        tile_row = y >> 3;
        for (x = 0; x < GB_W; x++) {
            int px, tile_idx;
            U16 tile_addr;
            U8 lo, hi, bit, col;
            px = (x + scx) & 0xFF;
            tile_idx = g_gb.vram[map_base + tile_row * 32 + (px >> 3)];
            if (g_gb.bg_override_on && tile_row < 18 && (px >> 3) < 20) {
                tile_idx = g_gb.bg_override[tile_row * 20 + (px >> 3)];
            }
            if (lcdc & 0x10) {
                tile_addr = (U16)(tile_idx * 16);
            } else {
                tile_addr = (U16)(0x1000 + (S8)tile_idx * 16);
            }
            tile_addr = (U16)(tile_addr + (y & 7) * 2);
            lo = g_gb.vram[tile_addr];
            hi = g_gb.vram[tile_addr + 1];
            bit = (U8)(7 - (px & 7));
            col = (U8)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
            bgcol[x] = col;
        }
    } else {
        memset(bgcol, 0, sizeof(bgcol));
    }

    /* Fenster */
    if ((lcdc & 0x21) == 0x21 && !g_gb.bg_override_on && ly >= wy && wx <= 166) {
        U16 map_base;
        int wline, start;
        map_base = (lcdc & 0x40) ? 0x1C00 : 0x1800;
        wline = g_gb.window_line;
        start = (int)wx - 7;
        for (x = start < 0 ? 0 : start; x < GB_W; x++) {
            int px, tile_idx;
            U16 tile_addr;
            U8 lo, hi, bit;
            px = x - start;
            tile_idx = g_gb.vram[map_base + (wline >> 3) * 32 + (px >> 3)];
            if (lcdc & 0x10) {
                tile_addr = (U16)(tile_idx * 16);
            } else {
                tile_addr = (U16)(0x1000 + (S8)tile_idx * 16);
            }
            tile_addr = (U16)(tile_addr + (wline & 7) * 2);
            lo = g_gb.vram[tile_addr];
            hi = g_gb.vram[tile_addr + 1];
            bit = (U8)(7 - (px & 7));
            bgcol[x] = (U8)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
        }
        win_drawn = 1;
    }
    if (win_drawn) {
        g_gb.window_line++;
    }

    for (x = 0; x < GB_W; x++) {
        shade[x] = (U8)((bgp >> (bgcol[x] * 2)) & 3);
        layer[x] = 0;
    }

    /* Sprites */
    if ((lcdc & 0x02) && !g_gb.bg_override_on) {
        sprh = (lcdc & 0x04) ? 16 : 8;
        nspr = 0;
        for (i = 0; i < 40 && nspr < 10; i++) {
            int sy;
            sy = (int)g_gb.oam[i * 4] - 16;
            if (ly >= sy && ly < sy + sprh) {
                spr_idx[nspr++] = i;
            }
        }
        /* Prioritaet DMG: kleinere X zuerst, bei Gleichstand kleinerer Index.
         * Zeichnen in umgekehrter Reihenfolge, damit die hoechste Prioritaet oben liegt. */
        for (i = 0; i < nspr; i++) {
            int j;
            for (j = i + 1; j < nspr; j++) {
                int a, b;
                a = spr_idx[i];
                b = spr_idx[j];
                if (g_gb.oam[b * 4 + 1] < g_gb.oam[a * 4 + 1]
                    || (g_gb.oam[b * 4 + 1] == g_gb.oam[a * 4 + 1] && b < a)) {
                    spr_idx[i] = b;
                    spr_idx[j] = a;
                }
            }
        }
        for (i = nspr - 1; i >= 0; i--) {
            const U8 *o;
            int sy, sx, row, tile;
            U8 attr, lo, hi, pal;
            o = &g_gb.oam[spr_idx[i] * 4];
            sy = (int)o[0] - 16;
            sx = (int)o[1] - 8;
            tile = o[2];
            attr = o[3];
            row = ly - sy;
            if (attr & 0x40) {
                row = sprh - 1 - row;
            }
            if (sprh == 16) {
                tile &= 0xFE;
            }
            lo = g_gb.vram[tile * 16 + row * 2];
            hi = g_gb.vram[tile * 16 + row * 2 + 1];
            pal = (attr & 0x10) ? obp1 : obp0;
            for (x = 0; x < 8; x++) {
                int px, bit;
                U8 col;
                px = sx + x;
                if (px < 0 || px >= GB_W) {
                    continue;
                }
                bit = (attr & 0x20) ? x : 7 - x;
                col = (U8)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
                if (col == 0) {
                    continue;
                }
                if ((attr & 0x80) && bgcol[px] != 0) {
                    continue;
                }
                shade[px] = (U8)((pal >> (col * 2)) & 3);
                layer[px] = (U8)((attr & 0x10) ? 2 : 1);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Timing                                                              */
/* ------------------------------------------------------------------ */

static void stat_update(void)
{
    U8 stat, line;
    stat = g_gb.io[0x41];
    line = 0;
    if ((stat & 0x40) && (stat & 0x04)) line = 1;
    if ((stat & 0x08) && g_gb.ppu_mode == 0) line = 1;
    if ((stat & 0x10) && g_gb.ppu_mode == 1) line = 1;
    if ((stat & 0x20) && g_gb.ppu_mode == 2) line = 1;
    if (line && !g_gb.stat_line) {
        g_gb.io[0x0F] |= INT_STAT;
    }
    g_gb.stat_line = line;
}

static void set_mode(U8 mode)
{
    g_gb.ppu_mode = mode;
    g_gb.io[0x41] = (U8)((g_gb.io[0x41] & 0xFC) | mode);
}

static void check_lyc(void)
{
    if (g_gb.io[0x44] == g_gb.io[0x45]) {
        g_gb.io[0x41] |= 0x04;
    } else {
        g_gb.io[0x41] &= (U8)~0x04;
    }
}

/* Zyklen bis zum naechsten Moduswechsel innerhalb der aktuellen Zeile */
static U32 next_transition(void)
{
    U8 ly;
    ly = g_gb.io[0x44];
    if (!(g_gb.io[0x40] & 0x80)) {
        return LINE_CYCLES;   /* LCD aus: nur Zeilenzaehler intern */
    }
    if (ly >= 144) {
        return LINE_CYCLES;
    }
    if (g_gb.ppu_line_cycle < MODE2_END) return MODE2_END;
    if (g_gb.ppu_line_cycle < MODE3_END) return MODE3_END;
    return LINE_CYCLES;
}

U32 gb_ppu_next_event(void)
{
    return g_gb.ppu_clock + (next_transition() - g_gb.ppu_line_cycle);
}

static U8 s_off_line;

void gb_ppu_sync(void)
{
    U32 elapsed, step, target;
    int lcd_on;

    elapsed = g_gb.cycles - g_gb.ppu_clock;
    if ((S32)elapsed <= 0) {
        return;
    }
    lcd_on = (g_gb.io[0x40] & 0x80) != 0;
    while (elapsed > 0) {
        target = next_transition();
        step = target - g_gb.ppu_line_cycle;
        if (step > elapsed) {
            g_gb.ppu_line_cycle += elapsed;
            g_gb.ppu_clock += elapsed;
            break;
        }
        elapsed -= step;
        g_gb.ppu_clock += step;
        g_gb.ppu_line_cycle = target;

        if (!lcd_on) {
            /* LCD aus: Frames im Hintergrund weiterzaehlen */
            g_gb.ppu_line_cycle = 0;
            s_off_line++;
            if (s_off_line >= 154) {
                s_off_line = 0;
            }
            if (s_off_line == 144) {
                memset(g_gb.shade, 0, sizeof(g_gb.shade));
                g_gb.lcd_was_off = 1;
                g_gb.frame_done = 1;
            }
            continue;
        }

        if (target == MODE2_END) {
            set_mode(3);
        } else if (target == MODE3_END) {
            render_line(g_gb.io[0x44]);
            set_mode(0);
            stat_update();
        } else {
            /* Zeilenende */
            g_gb.ppu_line_cycle = 0;
            g_gb.io[0x44]++;
            if (g_gb.io[0x44] == 154) {
                g_gb.io[0x44] = 0;
                g_gb.window_line = 0;
            }
            check_lyc();
            if (g_gb.io[0x44] == 144) {
                set_mode(1);
                g_gb.io[0x0F] |= INT_VBLANK;
                g_gb.frame_done = 1;
            } else if (g_gb.io[0x44] < 144) {
                set_mode(2);
            }
            stat_update();
        }
    }
}

void gb_ppu_write(U16 addr, U8 val)
{
    U8 reg;
    reg = (U8)(addr & 0x7F);
    switch (reg) {
    case 0x40: {
        U8 old;
        old = g_gb.io[0x40];
        g_gb.io[0x40] = val;
        if ((old & 0x80) && !(val & 0x80)) {
            g_gb.io[0x44] = 0;
            g_gb.ppu_line_cycle = 0;
            s_off_line = 0;
            set_mode(0);
        } else if (!(old & 0x80) && (val & 0x80)) {
            g_gb.io[0x44] = 0;
            g_gb.ppu_line_cycle = 0;
            g_gb.window_line = 0;
            set_mode(2);
            check_lyc();
        }
        break;
    }
    case 0x41:
        g_gb.io[0x41] = (U8)((g_gb.io[0x41] & 0x07) | (val & 0x78));
        break;
    case 0x44:
        break;
    case 0x45:
        g_gb.io[0x45] = val;
        check_lyc();
        stat_update();
        break;
    default:
        g_gb.io[reg] = val;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Bild zusammensetzen                                                 */
/* ------------------------------------------------------------------ */

void gb_ppu_compose_frame(void)
{
    int i, x, y;
    U16 *out;

    out = g_gb.frame;
    if (g_gb.sgb.mask == 1) {
        /* eingefroren: altes Bild behalten */
        memcpy(out, s_prev_frame, sizeof(s_prev_frame));
        return;
    }
    switch (g_gb.color_mode) {
    case COLOR_DMG:
        for (i = 0; i < GB_W * GB_H; i++) {
            out[i] = s_dmg_pal[g_gb.shade[i]];
        }
        break;
    case COLOR_GBC:
        for (i = 0; i < GB_W * GB_H; i++) {
            U8 l;
            l = g_gb.layer[i];
            out[i] = (l == 0) ? s_cgb_bg[g_gb.shade[i]]
                   : (l == 1) ? s_cgb_obj0[g_gb.shade[i]]
                   : s_cgb_obj1[g_gb.shade[i]];
        }
        break;
    default: {
        U16 pal565[4][4];
        int p, c;
        for (p = 0; p < 4; p++) {
            for (c = 0; c < 4; c++) {
                extern U16 gb_bgr15_to_rgb565(U16 c15);
                pal565[p][c] = gb_bgr15_to_rgb565(g_gb.sgb.pal[p][c]);
            }
        }
        for (y = 0; y < GB_H; y++) {
            for (x = 0; x < GB_W; x++) {
                U8 a;
                a = g_gb.sgb.attr[y >> 3][x >> 3] & 3;
                out[y * GB_W + x] = pal565[a][g_gb.shade[y * GB_W + x]];
            }
        }
        break;
    }
    }
    if (g_gb.sgb.mask == 2) {
        memset(out, 0, sizeof(g_gb.frame));
    } else if (g_gb.sgb.mask == 3) {
        U16 c0;
        c0 = (g_gb.color_mode == COLOR_SGB) ? gb_bgr15_to_rgb565(g_gb.sgb.pal[0][0]) : s_dmg_pal[0];
        for (i = 0; i < GB_W * GB_H; i++) {
            out[i] = c0;
        }
    }
    memcpy(s_prev_frame, out, sizeof(s_prev_frame));
    (void)C24(0);
}
