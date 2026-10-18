#include <stdlib.h>
/*
 * ui.c - oberer und unterer Bildschirm.
 *
 * Alle Grafiken (Schrift, Team-Icons, Weltkarte) und alle Spielwerte
 * (Team, Karte, Orden ...) werden zur Laufzeit ueber die Symbole der
 * Decomp aus ROM und WRAM gelesen (syms.h wird beim Build erzeugt).
 */
#include <string.h>
#include "ui.h"
#include "syms.h"

UiState g_ui;

/* ------------------------------------------------------------------ */
/* Farben (RGB565)                                                     */
/* ------------------------------------------------------------------ */

#define RGB(r, g, b) ((U16)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

#define COL_BG        RGB(0x22, 0x33, 0x4c)
#define COL_BG2       RGB(0x2a, 0x40, 0x5e)
#define COL_PANEL     RGB(0xf6, 0xf2, 0xe6)
#define COL_PANEL_DIM RGB(0x55, 0x68, 0x82)
#define COL_PANEL_SEL RGB(0xfb, 0xe0, 0x88)
#define COL_BORDER    RGB(0x24, 0x24, 0x2c)
#define COL_TEXT      RGB(0x30, 0x30, 0x38)
#define COL_TEXT_L    RGB(0xf0, 0xf0, 0xf0)
#define COL_TEXT_DIM  RGB(0x88, 0x88, 0x90)
#define COL_TAB       RGB(0x3c, 0x52, 0x74)
#define COL_HP_BG     RGB(0x48, 0x48, 0x50)
#define COL_HP_G      RGB(0x40, 0xc8, 0x58)
#define COL_HP_Y      RGB(0xf8, 0xc8, 0x30)
#define COL_HP_R      RGB(0xf0, 0x50, 0x38)
#define COL_ACCENT    RGB(0xe8, 0x40, 0x40)
#define COL_TOPBG     RGB(0x10, 0x10, 0x14)

/* Weltkarte: weiss = Meer, hell = Land, dunkel = Wege/Staedte */
static const U16 k_map_pal[4] = {
    RGB(0xf4, 0xec, 0xc8), RGB(0x80, 0xc0, 0xf0), RGB(0x60, 0xa8, 0x50), RGB(0x28, 0x30, 0x28)
};
static const U16 k_icon_pal[4] = {
    0, RGB(0xa8, 0xa8, 0xb0), RGB(0x58, 0x58, 0x68), RGB(0x18, 0x18, 0x20)
};

/* ------------------------------------------------------------------ */
/* Hilfen: ROM/WRAM                                                    */
/* ------------------------------------------------------------------ */

static U8 rom(U8 bank, U16 addr)
{
    return gb_rom_read_banked(bank, addr);
}

static U8 wram(U16 addr)
{
    return g_gb.wram[(addr - 0xC000) & 0x1FFF];
}

static U16 wram16be(U16 addr)
{
    return (U16)((wram(addr) << 8) | wram((U16)(addr + 1)));
}

/* ------------------------------------------------------------------ */
/* Zeichnen                                                            */
/* ------------------------------------------------------------------ */

static U16 *s_fb;
static int s_w, s_h;

static void target(U16 *fb, int w, int h)
{
    s_fb = fb;
    s_w = w;
    s_h = h;
}

static void fill(int x, int y, int w, int h, U16 c)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s_w) w = s_w - x;
    if (y + h > s_h) h = s_h - y;
    for (j = 0; j < h; j++) {
        U16 *p;
        p = &s_fb[(y + j) * s_w + x];
        for (i = 0; i < w; i++) {
            p[i] = c;
        }
    }
}

static void pset(int x, int y, U16 c)
{
    if (x >= 0 && y >= 0 && x < s_w && y < s_h) {
        s_fb[y * s_w + x] = c;
    }
}

/* Panel mit abgerundeten Ecken (1 px) und Rand */
static void panel(int x, int y, int w, int h, U16 bg, U16 border)
{
    fill(x + 1, y, w - 2, 1, border);
    fill(x + 1, y + h - 1, w - 2, 1, border);
    fill(x, y + 1, 1, h - 2, border);
    fill(x + w - 1, y + 1, 1, h - 2, border);
    fill(x + 1, y + 1, w - 2, h - 2, bg);
}

/* Spielzeichen (0x80-0xFF) aus FontGraphics (1bpp), skaliert */
static void glyph(int x, int y, U8 code, U16 col, int scale)
{
    int row, bit, sx, sy;
    U16 base;
    if (code < 0x80) {
        return;
    }
    base = (U16)(SYM_FontGraphics + (code - 0x80) * 8);
    for (row = 0; row < 8; row++) {
        U8 bits;
        bits = rom(BANK_FontGraphics, (U16)(base + row));
        for (bit = 0; bit < 8; bit++) {
            if (bits & (0x80 >> bit)) {
                for (sy = 0; sy < scale; sy++) {
                    for (sx = 0; sx < scale; sx++) {
                        pset(x + bit * scale + sx, y + row * scale + sy, col);
                    }
                }
            }
        }
    }
}

/* Spieltext (mit 0x50 = Ende) */
static int text_game(int x, int y, const U8 *s, int maxlen, U16 col, int scale)
{
    int i;
    for (i = 0; i < maxlen && s[i] != 0x50; i++) {
        glyph(x + i * 8 * scale, y, s[i], col, scale);
    }
    return i * 8 * scale;
}

/* UTF-8 -> Zeichensatz des Spiels */
static int encode(const char *utf8, U8 *out, int max)
{
    const U8 *s;
    int n;
    s = (const U8 *)utf8;
    n = 0;
    while (*s && n < max - 1) {
        U8 c, g;
        c = *s++;
        g = 0x7F;
        if (c >= 'A' && c <= 'Z') g = (U8)(0x80 + c - 'A');
        else if (c >= 'a' && c <= 'z') g = (U8)(0xA0 + c - 'a');
        else if (c >= '0' && c <= '9') g = (U8)(0xF6 + c - '0');
        else if (c == ' ') g = 0x7F;
        else if (c == '(') g = 0x9A;
        else if (c == ')') g = 0x9B;
        else if (c == ':') g = 0x9C;
        else if (c == '-') g = 0xE3;
        else if (c == '+') g = 0xE4;
        else if (c == '?') g = 0xE6;
        else if (c == '!') g = 0xE7;
        else if (c == '.') g = 0xE8;
        else if (c == '/') g = 0xF3;
        else if (c == ',') g = 0xF4;
        else if (c == '\'') g = 0xE0;
        else if (c == 0xC3 && *s) {
            U8 d;
            d = *s++;
            switch (d) {
            case 0x84: g = 0xC0; break; /* Ä */
            case 0x96: g = 0xC1; break; /* Ö */
            case 0x9C: g = 0xC2; break; /* Ü */
            case 0xA4: g = 0xC3; break; /* ä */
            case 0xB6: g = 0xC4; break; /* ö */
            case 0xBC: g = 0xC5; break; /* ü */
            case 0x9F: g = 0xBE; break; /* ß */
            case 0xA9: g = 0xBC; break; /* é */
            case 0x97: g = 0xF1; break; /* × */
            default: break;
            }
        } else if (c == 0xC2 && *s) {
            U8 d;
            d = *s++;
            if (d == 0xA5) g = 0xF0;    /* ¥ */
        } else if (c == 0xE2 && s[0] && s[1]) {
            if (s[0] == 0x99 && s[1] == 0x82) g = 0xEF; /* ♂ */
            else if (s[0] == 0x99 && s[1] == 0x80) g = 0xF5; /* ♀ */
            else if (s[0] == 0x96 && s[1] == 0xB6) g = 0xED; /* ▶ */
            s += 2;
        }
        out[n++] = g;
    }
    out[n] = 0x50;
    return n;
}

static int text(int x, int y, const char *utf8, U16 col, int scale)
{
    U8 buf[64];
    int n;
    n = encode(utf8, buf, sizeof(buf));
    return text_game(x, y, buf, n, col, scale);
}

static int text_width(const char *utf8, int scale)
{
    U8 buf[64];
    return encode(utf8, buf, sizeof(buf)) * 8 * scale;
}

static void text_center(int cx, int y, const char *utf8, U16 col, int scale)
{
    text(cx - text_width(utf8, scale) / 2, y, utf8, col, scale);
}

static void utoa_pad(U32 v, char *out, int width)
{
    char tmp[12];
    int n, i;
    n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 11);
    i = 0;
    while (n + i < width) {
        out[i++] = ' ';
    }
    while (n > 0) {
        out[i++] = tmp[--n];
    }
    out[i] = 0;
}

/* 2bpp-Kachel aus dem ROM (Gameboy-Format) */
static void tile2bpp(int x, int y, U8 bank, U16 addr, const U16 *pal, int xflip, int scale)
{
    int row, px, sx, sy;
    for (row = 0; row < 8; row++) {
        U8 lo, hi;
        lo = rom(bank, (U16)(addr + row * 2));
        hi = rom(bank, (U16)(addr + row * 2 + 1));
        for (px = 0; px < 8; px++) {
            int bit, c;
            bit = xflip ? px : 7 - px;
            c = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
            if (c == 0 && pal[0] == 0) {
                continue;
            }
            for (sy = 0; sy < scale; sy++) {
                for (sx = 0; sx < scale; sx++) {
                    pset(x + px * scale + sx, y + row * scale + sy, pal[c]);
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Team-Icons (wie im Teammenue des Spiels)                            */
/* ------------------------------------------------------------------ */

enum { ICON_MON, ICON_BALL, ICON_HELIX, ICON_FAIRY, ICON_BIRD, ICON_WATER,
       ICON_BUG, ICON_GRASS, ICON_SNAKE, ICON_QUADRUPED };

typedef struct {
    U8  bank;
    U16 addr;
    U8  top, bottom;    /* Kachelindizes linke Spalte; rechts gespiegelt */
} IconFrame;

static void icon_frame(int type, int frame, IconFrame *f)
{
    switch (type) {
    case ICON_BALL:
        f->bank = BANK_PokeBallSprite; f->addr = SYM_PokeBallSprite; f->top = 0; f->bottom = 2; break;
    case ICON_FAIRY:
        f->bank = BANK_FairySprite; f->addr = SYM_FairySprite; f->top = frame ? 0 : 12; f->bottom = (U8)(f->top + 2); break;
    case ICON_BIRD:
        f->bank = BANK_BirdSprite; f->addr = SYM_BirdSprite; f->top = frame ? 0 : 12; f->bottom = (U8)(f->top + 2); break;
    case ICON_WATER:
        f->bank = BANK_SeelSprite; f->addr = SYM_SeelSprite; f->top = frame ? 12 : 0; f->bottom = (U8)(f->top + 2); break;
    case ICON_BUG:
        f->bank = BANK_BugIconFrame1; f->addr = frame ? SYM_BugIconFrame1 : SYM_BugIconFrame2; f->top = 0; f->bottom = 1; break;
    case ICON_GRASS:
        f->bank = BANK_PlantIconFrame1; f->addr = frame ? SYM_PlantIconFrame1 : SYM_PlantIconFrame2; f->top = 0; f->bottom = 1; break;
    case ICON_SNAKE:
        f->bank = BANK_SnakeIconFrame1; f->addr = frame ? SYM_SnakeIconFrame2 : SYM_SnakeIconFrame1; f->top = 0; f->bottom = 1; break;
    case ICON_QUADRUPED:
        f->bank = BANK_QuadrupedIconFrame1; f->addr = frame ? SYM_QuadrupedIconFrame2 : SYM_QuadrupedIconFrame1; f->top = 0; f->bottom = 1; break;
    default:
        f->bank = BANK_MonsterSprite; f->addr = SYM_MonsterSprite; f->top = frame ? 0 : 12; f->bottom = (U8)(f->top + 2); break;
    }
}

static int species_icon(U8 species)
{
    U8 dex, nyb;
    if (species == 0 || species > 190) {
        return ICON_MON;
    }
    dex = rom(BANK_PokedexOrder, (U16)(SYM_PokedexOrder + species - 1));
    if (dex == 0) {
        return ICON_MON;
    }
    nyb = rom(BANK_MonPartyData, (U16)(SYM_MonPartyData + (dex - 1) / 2));
    return ((dex - 1) & 1) ? (nyb & 0x0F) : (nyb >> 4);
}

static void draw_icon(int x, int y, int type, int frame, int scale)
{
    if (type == ICON_HELIX) {
        /* asymmetrisch: 4 eigene Kacheln */
        U16 a;
        int dy;
        a = (U16)(SYM_PokeBallSprite + 4 * 16);
        dy = frame ? -scale : 0;
        tile2bpp(x, y + dy, BANK_PokeBallSprite, a, k_icon_pal, 0, scale);
        tile2bpp(x + 8 * scale, y + dy, BANK_PokeBallSprite, (U16)(a + 16), k_icon_pal, 0, scale);
        tile2bpp(x, y + 8 * scale + dy, BANK_PokeBallSprite, (U16)(a + 32), k_icon_pal, 0, scale);
        tile2bpp(x + 8 * scale, y + 8 * scale + dy, BANK_PokeBallSprite, (U16)(a + 48), k_icon_pal, 0, scale);
        return;
    }
    {
        IconFrame f;
        int dy;
        icon_frame(type, frame, &f);
        dy = (type == ICON_BALL && frame) ? -scale : 0;
        tile2bpp(x, y + dy, f.bank, (U16)(f.addr + f.top * 16), k_icon_pal, 0, scale);
        tile2bpp(x + 8 * scale, y + dy, f.bank, (U16)(f.addr + f.top * 16), k_icon_pal, 1, scale);
        tile2bpp(x, y + 8 * scale + dy, f.bank, (U16)(f.addr + f.bottom * 16), k_icon_pal, 0, scale);
        tile2bpp(x + 8 * scale, y + 8 * scale + dy, f.bank, (U16)(f.addr + f.bottom * 16), k_icon_pal, 1, scale);
    }
}

/* ------------------------------------------------------------------ */
/* Oberer Bildschirm                                                   */
/* ------------------------------------------------------------------ */

static U16 blend(U16 a, U16 b)
{
    /* Mittelwert zweier RGB565-Farben */
    return (U16)(((a & 0xF7DE) >> 1) + ((b & 0xF7DE) >> 1));
}

static void draw_sgb_border(U16 *fb, int ox, int oy)
{
    int ty, tx, py, px;
    U16 pal565[64];
    extern U16 gb_bgr15_to_rgb565(U16 c);

    for (px = 0; px < 64; px++) {
        pal565[px] = gb_bgr15_to_rgb565(g_gb.sgb.border_pal[px]);
    }
    for (ty = 0; ty < 28; ty++) {
        for (tx = 0; tx < 32; tx++) {
            U16 ent;
            const U8 *t;
            int pal, xf, yf;
            ent = g_gb.sgb.border_map[ty * 32 + tx];
            t = &g_gb.sgb.border_tiles[(ent & 0xFF) * 32];
            pal = ((ent >> 10) & 7);
            pal = (pal >= 4) ? (pal - 4) * 16 : 0;
            xf = (ent >> 14) & 1;
            yf = (ent >> 15) & 1;
            for (py = 0; py < 8; py++) {
                int r;
                U8 b0, b1, b2, b3;
                r = yf ? 7 - py : py;
                b0 = t[r * 2];
                b1 = t[r * 2 + 1];
                b2 = t[16 + r * 2];
                b3 = t[16 + r * 2 + 1];
                for (px = 0; px < 8; px++) {
                    int bit, c;
                    bit = xf ? px : 7 - px;
                    c = ((b0 >> bit) & 1) | (((b1 >> bit) & 1) << 1)
                      | (((b2 >> bit) & 1) << 2) | (((b3 >> bit) & 1) << 3);
                    if (c) {
                        fb[(oy + ty * 8 + py) * TOP_W + ox + tx * 8 + px] = pal565[pal + c];
                    }
                }
            }
        }
    }
}

static void blit_1x(U16 *fb, int ox, int oy)
{
    int y;
    for (y = 0; y < GB_H; y++) {
        memcpy(&fb[(oy + y) * TOP_W + ox], &g_gb.frame[y * GB_W], GB_W * 2);
    }
}

/* 1,5x: aus 2 Quellpixeln werden 3 (Mitte gemischt oder wiederholt) */
static void blit_15x(U16 *fb, int ox, int oy, int smooth)
{
    static U16 row[3][240];
    int y, x, k;
    for (y = 0; y < GB_H; y += 2) {
        for (k = 0; k < 2; k++) {
            const U16 *src;
            U16 *dst;
            src = &g_gb.frame[(y + k) * GB_W];
            dst = row[k * 2];
            for (x = 0; x < GB_W; x += 2) {
                dst[0] = src[x];
                dst[1] = smooth ? blend(src[x], src[x + 1]) : src[x];
                dst[2] = src[x + 1];
                dst += 3;
            }
        }
        for (x = 0; x < 240; x++) {
            row[1][x] = smooth ? blend(row[0][x], row[2][x]) : row[0][x];
        }
        for (k = 0; k < 3; k++) {
            memcpy(&fb[(oy + (y / 2) * 3 + k) * TOP_W + ox], row[k], 240 * 2);
        }
    }
}

void ui_draw_top(U16 *fb)
{
    int i;
    U16 bg;

    bg = COL_TOPBG;
    for (i = 0; i < TOP_W * TOP_H; i++) {
        fb[i] = bg;
    }
    if (g_ui.scale_mode == SCALE_BORDER) {
        if (g_gb.color_mode == COLOR_SGB && g_gb.sgb.border_valid) {
            blit_1x(fb, 72 + 48, 8 + 40);
            draw_sgb_border(fb, 72, 8);
        } else {
            blit_1x(fb, (TOP_W - GB_W) / 2, (TOP_H - GB_H) / 2);
        }
    } else {
        blit_15x(fb, (TOP_W - 240) / 2, (TOP_H - 216) / 2, g_ui.scale_mode == SCALE_SMOOTH15);
    }
}

/* ------------------------------------------------------------------ */
/* Unterer Bildschirm                                                  */
/* ------------------------------------------------------------------ */

static const char *k_tab_names[TAB_COUNT] = { "TEAM", "KARTE", "OPTIONEN" };

#define TAB_H 26

static void draw_background(void)
{
    int y;
    for (y = 0; y < BOT_H; y++) {
        /* dezente Streifen wie im DS-Menue */
        fill(0, y, BOT_W, 1, ((y >> 3) & 1) ? COL_BG : COL_BG2);
    }
}

static void draw_tabs(void)
{
    int i, w;
    w = BOT_W / TAB_COUNT;
    fill(0, 0, BOT_W, TAB_H, COL_BORDER);
    for (i = 0; i < TAB_COUNT; i++) {
        int x, active;
        x = i * w;
        active = (i == g_ui.tab);
        panel(x + 2, 2, w - 4, TAB_H - 2, active ? COL_PANEL : COL_TAB, COL_BORDER);
        if (active) {
            fill(x + 6, TAB_H - 3, w - 12, 2, COL_ACCENT);
        }
        text_center(x + w / 2, 9, k_tab_names[i], active ? COL_TEXT : COL_TEXT_L, 1);
    }
}

static void status_text(U8 st, char *out)
{
    out[0] = 0;
    if (st & 0x07) strcpy(out, "SLF");
    else if (st & 0x08) strcpy(out, "GIF");
    else if (st & 0x10) strcpy(out, "BRT");
    else if (st & 0x20) strcpy(out, "GFR");
    else if (st & 0x40) strcpy(out, "PAR");
}

static void draw_team(void)
{
    int count, i, frame;
    U16 stride;

    count = wram(SYM_wPartyCount);
    if (count > 6) count = 6;
    stride = (U16)(SYM_wPartyMon2 - SYM_wPartyMon1);
    frame = (g_ui.frame_counter >> 4) & 1;

    for (i = 0; i < 6; i++) {
        int px, py, pw, ph;
        px = 6 + (i & 1) * 156;
        py = TAB_H + 6 + (i >> 1) * 70;
        pw = 152;
        ph = 66;
        if (i >= count) {
            panel(px, py, pw, ph, COL_PANEL_DIM, COL_BORDER);
            continue;
        }
        {
            U16 base, hp, maxhp;
            U8 species, level, st;
            char buf[24], num[8];
            int barw, fillw;
            U16 barcol;

            base = (U16)(SYM_wPartyMon1 + i * stride);
            species = wram(SYM_wPartySpecies + i);
            hp = wram16be((U16)(base + (SYM_wPartyMon1HP - SYM_wPartyMon1)));
            maxhp = wram16be((U16)(base + (SYM_wPartyMon1MaxHP - SYM_wPartyMon1)));
            level = wram((U16)(base + (SYM_wPartyMon1Level - SYM_wPartyMon1)));
            st = wram((U16)(base + (SYM_wPartyMon1Status - SYM_wPartyMon1)));
            if (wram(SYM_wIsInBattle) && wram(SYM_wPlayerMonNumber) == i) {
                hp = wram16be(SYM_wBattleMonHP);
                st = wram(SYM_wBattleMonStatus);
            }

            panel(px, py, pw, ph, (i == 0 || g_ui.touched_slot == i) ? COL_PANEL_SEL : COL_PANEL, COL_BORDER);
            /* Icon-Kreis */
            panel(px + 5, py + 6, 38, 38, RGB(0xe4, 0xe0, 0xd4), RGB(0xc0, 0xbc, 0xb0));
            draw_icon(px + 8, py + 9, species_icon(species), hp ? frame : 0, 2);

            text_game(px + 48, py + 7, &g_gb.wram[(SYM_wPartyMonNicks + i * 11) - 0xC000], 10, COL_TEXT, 1);

            strcpy(buf, "Lv");
            utoa_pad(level, num, 0);
            strcat(buf, num);
            text(px + 48, py + 19, buf, COL_TEXT, 1);
            status_text(st, buf);
            if (hp == 0) {
                strcpy(buf, "K.O.");
            }
            if (buf[0]) {
                panel(px + 104, py + 17, 42, 12, hp ? COL_ACCENT : COL_TEXT_DIM, COL_BORDER);
                text_center(px + 125, py + 19, buf, COL_TEXT_L, 1);
            }

            /* KP-Balken */
            barw = 74;
            fillw = maxhp ? (int)((U32)hp * (U32)barw / maxhp) : 0;
            if (hp && fillw == 0) fillw = 1;
            barcol = (hp * 2 > maxhp) ? COL_HP_G : (hp * 5 > maxhp) ? COL_HP_Y : COL_HP_R;
            text(px + 48, py + 33, "KP", COL_TEXT, 1);
            panel(px + 66, py + 33, barw + 4, 8, COL_HP_BG, COL_BORDER);
            fill(px + 68, py + 35, fillw, 4, barcol);

            utoa_pad(hp, num, 3);
            strcpy(buf, num);
            strcat(buf, "/");
            utoa_pad(maxhp, num, 3);
            strcat(buf, num);
            text(px + pw - 8 - text_width(buf, 1), py + 47, buf, COL_TEXT, 1);
        }
    }
    if (count == 0) {
        panel(40, 100, 240, 40, COL_PANEL, COL_BORDER);
        text_center(160, 116, "Noch keine POK\xc3\xa9MON", COL_TEXT, 1);
    }
}

/* Weltkarte aus CompressedMap (RLE) und WorldMapTileGraphics */
static U8 s_map_tiles[18 * 20];
static int s_map_loaded;

static void load_town_map(void)
{
    U16 a;
    int n;
    n = 0;
    a = SYM_CompressedMap;
    while (n < 360) {
        U8 v;
        int cnt;
        v = rom(BANK_CompressedMap, a++);
        if (v == 0) break;
        for (cnt = v & 0x0F; cnt > 0 && n < 360; cnt--) {
            s_map_tiles[n++] = (U8)(v >> 4);
        }
    }
    while (n < 360) s_map_tiles[n++] = 0;
    s_map_loaded = 1;
}

static void map_entry(U8 map, U8 *x, U8 *y, U16 *name)
{
    U16 a;
    U8 yx;
    if (map < 0x25) {   /* FIRST_INDOOR_MAP */
        a = (U16)(SYM_ExternalMapEntries + map * 3);
    } else {
        a = SYM_InternalMapEntries;
        while (rom(BANK_InternalMapEntries, a) != 0xFF && map >= rom(BANK_InternalMapEntries, a)) {
            a = (U16)(a + 4);
        }
        a++;
    }
    yx = rom(BANK_ExternalMapEntries, a);
    *x = (U8)(yx & 0x0F);
    *y = (U8)(yx >> 4);
    *name = (U16)(rom(BANK_ExternalMapEntries, (U16)(a + 1)) | (rom(BANK_ExternalMapEntries, (U16)(a + 2)) << 8));
}

static void draw_map(void)
{
    int tx, ty, ox, oy;
    U8 mx, my, map;
    U16 name;
    U8 namebuf[24];
    int i;

    if (!s_map_loaded) {
        load_town_map();
    }
    ox = 40;
    oy = TAB_H - 2;
    /* Karte 1,5x: jede 8x8-Kachel wird 12x12 */
    for (ty = 0; ty < 18; ty++) {
        for (tx = 0; tx < 20; tx++) {
            U16 ta;
            int r, p;
            ta = (U16)(SYM_WorldMapTileGraphics + s_map_tiles[ty * 20 + tx] * 16);
            for (r = 0; r < 12; r++) {
                int sr;
                U8 lo, hi;
                sr = (r * 2) / 3;
                lo = rom(BANK_WorldMapTileGraphics, (U16)(ta + sr * 2));
                hi = rom(BANK_WorldMapTileGraphics, (U16)(ta + sr * 2 + 1));
                for (p = 0; p < 12; p++) {
                    int sp, c;
                    sp = 7 - (p * 2) / 3;
                    c = ((lo >> sp) & 1) | (((hi >> sp) & 1) << 1);
                    pset(ox + tx * 12 + p, oy + ty * 12 + r, k_map_pal[c]);
                }
            }
        }
    }

    map = wram(SYM_wCurMap);
    map_entry(map, &mx, &my, &name);
    /* Spielerposition: wie im Spiel (x*8+16, y*8+8) als 16x16 */
    {
        int sx, sy, blink;
        sx = ox + ((mx * 8 + 16) * 3) / 2;
        sy = oy + ((my * 8 + 8) * 3) / 2;
        blink = (g_ui.frame_counter >> 4) & 1;
        if (blink) {
            panel(sx - 2, sy - 2, 28, 28, COL_ACCENT, COL_BORDER);
        }
        tile2bpp(sx, sy, BANK_RedSprite, (U16)(SYM_RedSprite), k_icon_pal, 0, 1);
        tile2bpp(sx + 8, sy, BANK_RedSprite, (U16)(SYM_RedSprite + 16), k_icon_pal, 0, 1);
        tile2bpp(sx, sy + 8, BANK_RedSprite, (U16)(SYM_RedSprite + 32), k_icon_pal, 0, 1);
        tile2bpp(sx + 8, sy + 8, BANK_RedSprite, (U16)(SYM_RedSprite + 48), k_icon_pal, 0, 1);
        (void)tile2bpp;
    }
    /* Ortsname */
    for (i = 0; i < 20; i++) {
        namebuf[i] = rom(BANK_ExternalMapEntries, (U16)(name + i));
        if (namebuf[i] == 0x50) break;
    }
    namebuf[20] = 0x50;
    panel(ox + 4, oy + 4, 232, 16, COL_PANEL, COL_BORDER);
    text_game(ox + 10, oy + 8, namebuf, 20, COL_TEXT, 1);
}

/* Optionen */
typedef struct {
    int x, y, w, h;
} Rect;

static Rect s_btn_color[3];
static Rect s_btn_scale[SCALE_COUNT];

static void button(Rect *r, int x, int y, int w, int h, const char *label, int active)
{
    r->x = x; r->y = y; r->w = w; r->h = h;
    panel(x, y, w, h, active ? COL_PANEL_SEL : COL_PANEL, COL_BORDER);
    if (active) {
        fill(x + 4, y + h - 4, w - 8, 2, COL_ACCENT);
    }
    text_center(x + w / 2, y + (h - 8) / 2, label, COL_TEXT, 1);
}

static int popcount_range(U16 addr, int bytes)
{
    int i, n;
    n = 0;
    for (i = 0; i < bytes; i++) {
        U8 v;
        v = wram((U16)(addr + i));
        while (v) {
            n += v & 1;
            v >>= 1;
        }
    }
    return n;
}

static void draw_options(void)
{
    static const char *cm[3] = { "DMG", "GBC", "SGB" };
    static const char *sm[SCALE_COUNT] = { "1,5x weich", "1x Rahmen", "1,5x scharf" };
    int i, y;
    char buf[40], num[12];

    y = TAB_H + 6;
    panel(6, y, 308, 54, COL_PANEL, COL_BORDER);
    text(14, y + 6, "Farbmodus", COL_TEXT, 1);
    for (i = 0; i < 3; i++) {
        button(&s_btn_color[i], 14 + i * 98, y + 20, 94, 28, cm[i], g_gb.color_mode == i);
    }
    y += 60;
    panel(6, y, 308, 54, COL_PANEL, COL_BORDER);
    text(14, y + 6, "Bild oben", COL_TEXT, 1);
    for (i = 0; i < SCALE_COUNT; i++) {
        button(&s_btn_scale[i], 14 + i * 98, y + 20, 94, 28, sm[i], g_ui.scale_mode == i);
    }
    y += 60;
    /* Trainerpass */
    panel(6, y, 308, BOT_H - y - 6, COL_PANEL, COL_BORDER);
    text(14, y + 6, "Name:", COL_TEXT, 1);
    text_game(70, y + 6, &g_gb.wram[SYM_wPlayerName - 0xC000], 7, COL_TEXT, 1);
    strcpy(buf, "Orden: ");
    utoa_pad((U32)popcount_range(SYM_wObtainedBadges, 1), num, 0);
    strcat(buf, num);
    text(170, y + 6, buf, COL_TEXT, 1);
    strcpy(buf, "Pok\xc3\xa9" "dex: ");
    utoa_pad((U32)popcount_range(SYM_wPokedexOwned, 19), num, 0);
    strcat(buf, num);
    text(14, y + 20, buf, COL_TEXT, 1);
    {
        U32 money;
        U16 a;
        a = SYM_wPlayerMoney;
        money = (U32)((wram(a) >> 4) * 100000 + (wram(a) & 15) * 10000
              + (wram((U16)(a + 1)) >> 4) * 1000 + (wram((U16)(a + 1)) & 15) * 100
              + (wram((U16)(a + 2)) >> 4) * 10 + (wram((U16)(a + 2)) & 15));
        strcpy(buf, "Geld: ");
        utoa_pad(money, num, 0);
        strcat(buf, num);
        strcat(buf, "\xc2\xa5");
        text(170, y + 20, buf, COL_TEXT, 1);
    }
    strcpy(buf, "Spielzeit: ");
    utoa_pad(wram(SYM_wPlayTimeHours), num, 0);
    strcat(buf, num);
    strcat(buf, ":");
    utoa_pad(wram(SYM_wPlayTimeMinutes), num, 2);
    if (num[0] == ' ') num[0] = '0';
    strcat(buf, num);
    text(14, y + 34, buf, COL_TEXT, 1);
    if (g_ui.audio_state == 1) {
        text(14, y + 48, "Ton: an", COL_TEXT, 1);
    } else if (g_ui.audio_state == 2) {
        text(14, y + 48, "Ton: aus (3ds/dspfirm.cdc fehlt)", COL_TEXT, 1);
    }
    if (g_ui.save_state == 1) {
        text(170, y + 34, "Speicher: Archiv", COL_TEXT, 1);
    } else if (g_ui.save_state == 2) {
        text(170, y + 34, "Speicher: SD", COL_TEXT, 1);
    } else if (g_ui.save_state == 3) {
        text(170, y + 34, "Speicher: FEHLER", COL_TEXT, 1);
    }
}

/* ------------------------------------------------------------------ */
/* Kampf-Menues auf dem unteren Bildschirm                             */
/*                                                                     */
/* Das Spiel laeuft unveraendert weiter und wartet in seiner eigenen   */
/* Menuefunktion (HandleMenuInput). Die UI erkennt am Game-Boy-Stapel, */
/* welches Menue gerade offen ist, spiegelt den Cursor aus dem WRAM    */
/* und zeichnet das Menue gross auf den Touchscreen. Ein Tipp wird in  */
/* Steuerkreuz- und A-Druecke uebersetzt, die wie echte Tasten an das  */
/* Spiel gehen. Steuerkreuz + A funktionieren daher genau wie vorher.  */
/* ------------------------------------------------------------------ */

#define BM_NONE     0
#define BM_MAIN     1
#define BM_MOVES    2
#define BM_ITEMS    3
#define BM_PARTY    4
#define BM_PARTYSUB 5
#define BM_WAIT     6   /* im Kampf, aber gerade kein Menue (Text, Animationen) */

#define HIT_UP    100
#define HIT_DOWN  101
#define HIT_BACK  102

#define BP_PAPER RGB(0xf6, 0xe9, 0xf7)
#define BP_INK   RGB(0x2a, 0x28, 0x38)
#define BP_MID   RGB(0x8f, 0x84, 0xa8)
#define BP_SEL   RGB(0xe9, 0xd2, 0xee)

#define ITEM_HM01 0xC4
#define ITEM_TM01 0xC9
#define ITEM_ROWS 6

static U8  s_bmode;       /* offenes Kampfmenue (BM_*) */
static int s_bcur;        /* Cursorposition im Menue */
static int s_bcount;      /* Anzahl Eintraege */
static int s_btop;        /* erste sichtbare Zeile der Item-Liste */

typedef struct {
    int x, y, w, h, id;
} BHit;

static BHit s_hit[16];
static int s_nhit;

/* Warteschlange virtueller Tasten (Touch -> Steuerkreuz/A/B) */
static U8 s_qbtn[24];
static U8 s_qlen, s_qpos, s_qtick, s_qmode;

/* Steht eine Ruecksprungadresse aus [lo,hi) im Stapel des Spiels?
 * Die ROM-Bank wird bewusst nicht geprueft: Am Frameende laeuft oft gerade
 * der VBlank-Handler mit einer anderen Bank (Ton). Stattdessen pruefen die
 * Aufrufer zusaetzlich Menue-Koordinaten im WRAM. */
static int stack_in(U16 lo, U16 hi)
{
    U16 a, v;
    for (a = g_gb.cpu.sp; a >= 0xC000 && a < 0xDFFE; a = (U16)(a + 2)) {
        v = (U16)(wram(a) | (wram((U16)(a + 1)) << 8));
        if (v >= lo && v < hi) {
            return 1;
        }
    }
    return 0;
}

static U8 detect_battle_mode(void)
{
    U8 inb;
    inb = wram(SYM_wIsInBattle);
    if (inb == 0 || inb == 0xFF || wram(SYM_wBattleType) != 0) {
        return BM_NONE;
    }
    if (stack_in(SYM_PartyMenuOrRockOrRun_partyMonWasSelected, SYM_PartyMenuOrRockOrRun_partyMonWasSelected_END)) {
        return BM_PARTYSUB;
    }
    if (stack_in(SYM_HandlePartyMenuInput, SYM_HandlePartyMenuInput_END)) {
        return BM_PARTY;
    }
    if (wram(SYM_wListMenuID) == 3 && stack_in(SYM_DisplayListMenuIDLoop, SYM_DisplayListMenuIDLoop_END)) {
        return BM_ITEMS;
    }
    if (wram(SYM_wMoveMenuType) == 0 && wram(SYM_wTopMenuItemY) == 12 &&
        stack_in(SYM_SelectMenuItem, SYM_SelectMenuItem_END)) {
        return BM_MOVES;
    }
    if (wram(SYM_wTopMenuItemY) == 14 && stack_in(SYM_DisplayBattleMenu, SYM_DisplayBattleMenu_END)) {
        return BM_MAIN;
    }
    return BM_WAIT;
}

static int move_count(void)
{
    int i, n;
    n = 0;
    for (i = 0; i < 4; i++) {
        if (wram((U16)(SYM_wBattleMonMoves + i))) {
            n++;
        }
    }
    return n;
}

static void battle_update(void)
{
    int n;
    s_bmode = detect_battle_mode();
    s_bcur = 0;
    s_bcount = 0;
    switch (s_bmode) {
    case BM_MAIN:
        s_bcount = 4;
        s_bcur = (wram(SYM_wCurrentMenuItem) & 1) * 2 + (wram(SYM_wTopMenuItemX) >= 10 ? 1 : 0);
        break;
    case BM_MOVES:
        s_bcount = move_count();
        s_bcur = (int)wram(SYM_wCurrentMenuItem) - 1;
        if (s_bcur < 0) s_bcur = 0;
        if (s_bcur >= s_bcount) s_bcur = s_bcount - 1;
        break;
    case BM_ITEMS:
        n = wram(SYM_wNumBagItems);
        if (n > 20) n = 20;
        s_bcount = n + 1;   /* letzte Zeile: ABBRECHEN */
        s_bcur = wram(SYM_wListScrollOffset) + wram(SYM_wCurrentMenuItem);
        if (s_bcur >= s_bcount) s_bcur = s_bcount - 1;
        break;
    case BM_PARTY:
        s_bcount = wram(SYM_wPartyCount);
        if (s_bcount > 6) s_bcount = 6;
        s_bcur = wram(SYM_wCurrentMenuItem);
        if (s_bcur >= s_bcount) s_bcur = s_bcount - 1;
        break;
    case BM_PARTYSUB:
        s_bcount = 3;
        s_bcur = wram(SYM_wCurrentMenuItem);
        if (s_bcur > 2) s_bcur = 2;
        break;
    default:
        break;
    }
}

/* Signatur der Kampfmenue-Daten fuer ui_bottom_needs_redraw() */
static U32 battle_signature(U32 h)
{
    U16 a;
    h = (h ^ s_bmode) * 16777619u;
    h = (h ^ (U32)s_bcur) * 16777619u;
    h = (h ^ (U32)s_bcount) * 16777619u;
    if (s_bmode == BM_NONE) {
        return h;
    }
    for (a = SYM_wBattleMonMoves; a < SYM_wBattleMonMoves + 4; a++) h = (h ^ wram(a)) * 16777619u;
    for (a = SYM_wBattleMonPP; a < SYM_wBattleMonPP + 4; a++) h = (h ^ wram(a)) * 16777619u;
    if (s_bmode == BM_ITEMS) {
        for (a = SYM_wNumBagItems; a < SYM_wNumBagItems + 42; a++) h = (h ^ wram(a)) * 16777619u;
        h = (h ^ wram(SYM_wListScrollOffset)) * 16777619u;
    }
    h = (h ^ wram(SYM_wWhichPokemon)) * 16777619u;
    if (s_bmode == BM_PARTY || s_bmode == BM_PARTYSUB) {
        h = (h ^ (g_ui.frame_counter >> 4)) * 16777619u;
    }
    return h;
}

/* ---- Namen und Werte aus dem ROM ---- */

/* Eintrag n (1-basiert) aus einer Liste 0x50-terminierter Namen */
static int rom_name(U8 bank, U16 base, int index, U8 *out, int max)
{
    U16 a;
    int n;
    U8 c;
    a = base;
    while (index > 1) {
        while (rom(bank, a) != 0x50) {
            a++;
        }
        a++;
        index--;
    }
    for (n = 0; n < max - 1; n++) {
        c = rom(bank, (U16)(a + n));
        if (c == 0x50) {
            break;
        }
        out[n] = c;
    }
    out[n] = 0x50;
    return n;
}

static int item_name(U8 id, U8 *out, int max)
{
    if (id >= ITEM_TM01) {
        char buf[8], num[8];
        strcpy(buf, "TM");
        utoa_pad((U32)(id - ITEM_TM01 + 1), num, 2);
        if (num[0] == ' ') num[0] = '0';
        strcat(buf, num);
        return encode(buf, out, max);
    }
    if (id >= ITEM_HM01) {
        char buf[8], num[8];
        strcpy(buf, "VM");
        utoa_pad((U32)(id - ITEM_HM01 + 1), num, 2);
        if (num[0] == ' ') num[0] = '0';
        strcat(buf, num);
        return encode(buf, out, max);
    }
    return rom_name(BANK_ItemNames, SYM_ItemNames, id, out, max);
}

static int type_name(U8 type, U8 *out, int max)
{
    U16 p;
    int n;
    U8 c;
    p = (U16)(rom(BANK_TypeNames, (U16)(SYM_TypeNames + type * 2)) |
              (rom(BANK_TypeNames, (U16)(SYM_TypeNames + type * 2 + 1)) << 8));
    for (n = 0; n < max - 1; n++) {
        c = rom(BANK_TypeNames, (U16)(p + n));
        if (c == 0x50) {
            break;
        }
        out[n] = c;
    }
    out[n] = 0x50;
    return n;
}

static U32 money_value(void)
{
    U32 v;
    int i;
    U8 b;
    v = 0;
    for (i = 0; i < 3; i++) {
        b = wram((U16)(SYM_wPlayerMoney + i));
        v = v * 100 + (U32)((b >> 4) * 10 + (b & 15));
    }
    return v;
}

/* ---- Zeichenhilfen im Stil des Spiels ---- */

static void gbframe(int x, int y, int w, int h, U16 bg)
{
    panel(x, y, w, h, BP_INK, BP_INK);
    panel(x + 2, y + 2, w - 4, h - 4, BP_PAPER, BP_PAPER);
    panel(x + 3, y + 3, w - 6, h - 6, bg, BP_MID);
}

static void add_hit(int x, int y, int w, int h, int id)
{
    if (s_bmode == BM_WAIT) {
        return;     /* im Wartezustand ist nichts antippbar */
    }
    if (s_nhit < 16) {
        s_hit[s_nhit].x = x;
        s_hit[s_nhit].y = y;
        s_hit[s_nhit].w = w;
        s_hit[s_nhit].h = h;
        s_hit[s_nhit].id = id;
        s_nhit++;
    }
}

static void cursor_glyph(int x, int y)
{
    glyph(x, y, 0xED, BP_INK, 2);
}

/* gefuelltes Dreieck: dir 0 = hoch, 1 = runter */
static void triangle(int cx, int y, int dir, U16 col)
{
    int i, wd;
    for (i = 0; i < 10; i++) {
        wd = dir ? (10 - i) : (i + 1);
        fill(cx - wd, y + i, wd * 2, 1, col);
    }
}

static void ball_icon(int x, int y, int state)
{
    /* 0 = gesund, 1 = Status, 2 = besiegt */
    int dx, dy;
    U16 col;
    for (dy = 0; dy < 11; dy++) {
        for (dx = 0; dx < 11; dx++) {
            int ox, oy;
            ox = dx - 5;
            oy = dy - 5;
            if (ox * ox + oy * oy > 30) {
                continue;
            }
            if (ox * ox + oy * oy > 18) {
                col = BP_INK;
            } else if (state == 2) {
                col = BP_PAPER;
            } else if (dy < 5) {
                col = BP_INK;
            } else {
                col = (state == 1) ? BP_MID : BP_PAPER;
            }
            pset(x + dx, y + dy, col);
        }
    }
}

static void hp_bar(int x, int y, int w, U16 hp, U16 maxhp)
{
    int fw;
    U16 col;
    fw = maxhp ? (int)((U32)hp * (U32)w / maxhp) : 0;
    if (hp && fw == 0) fw = 1;
    col = (hp * 2 > maxhp) ? COL_HP_G : (hp * 5 > maxhp) ? COL_HP_Y : COL_HP_R;
    panel(x, y, w + 4, 8, BP_PAPER, BP_INK);
    fill(x + 2, y + 2, fw, 4, col);
}

static void back_button(int x, int y, int w, int h)
{
    gbframe(x, y, w, h, BP_PAPER);
    /* Pfeil nach links aus 5 Spalten */
    {
        int i;
        for (i = 0; i < 6; i++) {
            fill(x + 12 + i, y + h / 2 - i, 1, i * 2 + 1, BP_INK);
        }
    }
    text(x + 26, y + h / 2 - 4, "ZUR\xc3\x9c" "CK", BP_INK, 1);
    add_hit(x, y, w, h, HIT_BACK);
}

/* Kopfzeile: aktives POKéMON mit Icon, Name, Level und KP */
static void draw_b_header(int y)
{
    U16 hp, maxhp;
    char buf[16], num[8];
    int tw;
    hp = wram16be(SYM_wBattleMonHP);
    maxhp = wram16be(SYM_wBattleMonMaxHP);
    gbframe(8, y, 304, 52, BP_PAPER);
    draw_icon(16, y + 10, species_icon(wram(SYM_wBattleMonSpecies)), hp ? (int)((g_ui.frame_counter >> 4) & 1) : 0, 2);
    text_game(56, y + 8, &g_gb.wram[SYM_wBattleMonNick - 0xC000], 10, BP_INK, 2);
    strcpy(buf, "Lv");
    utoa_pad(wram(SYM_wBattleMonLevel), num, 0);
    strcat(buf, num);
    tw = text_width(buf, 1);
    text(300 - tw, y + 12, buf, BP_INK, 1);
    text(56, y + 32, "KP", BP_INK, 1);
    hp_bar(76, y + 31, 124, hp, maxhp);
    utoa_pad(hp, num, 3);
    strcpy(buf, num);
    strcat(buf, "/");
    utoa_pad(maxhp, num, 3);
    strcat(buf, num);
    text(300 - text_width(buf, 1), y + 32, buf, BP_INK, 1);
}

static void draw_b_main(void)
{
    static const char *labels[4] = { "KAMPF", "POK\xc3\xa9MON", "ITEM", "FLUCHT" };
    int i, x, y, count, st;
    U16 base, hp;
    U16 stride;
    char buf[24], num[8];

    draw_b_header(8);
    for (i = 0; i < 4; i++) {
        x = 8 + (i & 1) * 156;
        y = 68 + (i >> 1) * 66;
        gbframe(x, y, 148, 58, i == s_bcur ? BP_SEL : BP_PAPER);
        if (i == s_bcur) {
            cursor_glyph(x + 8, y + 21);
        }
        text(x + 26, y + 21, labels[i], s_bmode == BM_WAIT ? BP_MID : BP_INK, 2);
        add_hit(x, y, 148, 58, i);
    }
    /* Fussleiste: Team (Baelle) und Gegner */
    count = wram(SYM_wPartyCount);
    if (count > 6) count = 6;
    stride = (U16)(SYM_wPartyMon2 - SYM_wPartyMon1);
    for (i = 0; i < 6; i++) {
        if (i < count) {
            base = (U16)(SYM_wPartyMon1 + i * stride);
            hp = wram16be((U16)(base + (SYM_wPartyMon1HP - SYM_wPartyMon1)));
            if (wram(SYM_wPlayerMonNumber) == i) {
                hp = wram16be(SYM_wBattleMonHP);
            }
            st = wram((U16)(base + (SYM_wPartyMon1Status - SYM_wPartyMon1))) ? 1 : 0;
            ball_icon(14 + i * 15, 208, hp == 0 ? 2 : st);
        } else {
            ball_icon(14 + i * 15, 208, 2);
        }
    }
    text_game(160, 211, &g_gb.wram[SYM_wEnemyMonNick - 0xC000], 10, BP_INK, 1);
    strcpy(buf, "Lv");
    utoa_pad(wram(SYM_wEnemyMonLevel), num, 0);
    strcat(buf, num);
    text(300 - text_width(buf, 1), 211, buf, BP_INK, 1);
}

static void draw_b_moves(void)
{
    int i, n, x, y, ppmax, ppnow, base;
    U8 id, type, ppraw;
    U8 name[16];
    char buf[16], num[8];

    n = move_count();
    draw_b_header(8);
    gbframe(8, 66, 304, 128, BP_PAPER);
    for (i = 0; i < 4; i++) {
        y = 74 + i * 28;
        id = wram((U16)(SYM_wBattleMonMoves + i));
        if (i < n) {
            if (i == s_bcur) {
                fill(14, y - 2, 292, 26, BP_SEL);
                cursor_glyph(18, y);
            }
            rom_name(BANK_MoveNames, SYM_MoveNames, id, name, sizeof(name));
            text_game(40, y, name, 12, BP_INK, 2);
            base = rom(0x0E, (U16)(SYM_Moves + (id - 1) * 6 + 5));
            ppraw = wram((U16)(SYM_wBattleMonPP + i));
            ppnow = ppraw & 0x3F;
            ppmax = base + (base / 5) * (ppraw >> 6);
            strcpy(buf, "AP ");
            utoa_pad((U32)ppnow, num, 2);
            strcat(buf, num);
            strcat(buf, "/");
            utoa_pad((U32)ppmax, num, 2);
            strcat(buf, num);
            text(300 - text_width(buf, 1), y + 4, buf, ppnow ? BP_INK : COL_ACCENT, 1);
            add_hit(8, y - 2, 304, 28, i);
        } else {
            text(40, y, "-", BP_MID, 2);
        }
    }
    /* Infoleiste: Typ und AP der gewaehlten Attacke */
    gbframe(8, 200, 196, 32, BP_PAPER);
    if (n > 0) {
        id = wram((U16)(SYM_wBattleMonMoves + s_bcur));
        type = rom(0x0E, (U16)(SYM_Moves + (id - 1) * 6 + 3));
        x = 18;
        x += text(x, 206, "TYP/", BP_INK, 1);
        type_name(type, name, sizeof(name));
        text_game(x, 206, name, 10, BP_INK, 1);
        base = rom(0x0E, (U16)(SYM_Moves + (id - 1) * 6 + 5));
        ppraw = wram((U16)(SYM_wBattleMonPP + s_bcur));
        ppnow = ppraw & 0x3F;
        ppmax = base + (base / 5) * (ppraw >> 6);
        strcpy(buf, "");
        utoa_pad((U32)ppnow, num, 2);
        strcat(buf, num);
        strcat(buf, "/");
        utoa_pad((U32)ppmax, num, 2);
        strcat(buf, num);
        text(18, 218, "AP", BP_INK, 1);
        text(196 - text_width(buf, 1), 218, buf, BP_INK, 1);
    }
    back_button(212, 200, 100, 32);
}

static void draw_b_items(void)
{
    int i, n, idx, y, qty;
    U8 id;
    U8 name[16];
    char buf[24], num[12];

    n = wram(SYM_wNumBagItems);
    if (n > 20) n = 20;
    if (s_btop > s_bcur) s_btop = s_bcur;
    if (s_bcur >= s_btop + ITEM_ROWS) s_btop = s_bcur - ITEM_ROWS + 1;
    if (s_btop < 0) s_btop = 0;

    gbframe(8, 8, 256, 156, BP_PAPER);
    for (i = 0; i < ITEM_ROWS; i++) {
        idx = s_btop + i;
        if (idx > n) {
            break;
        }
        y = 16 + i * 24;
        if (idx == s_bcur) {
            fill(14, y - 2, 244, 22, BP_SEL);
            cursor_glyph(18, y);
        }
        if (idx < n) {
            id = wram((U16)(SYM_wBagItems + idx * 2));
            qty = wram((U16)(SYM_wBagItems + idx * 2 + 1));
            item_name(id, name, sizeof(name));
            text_game(40, y, name, 10, BP_INK, 2);
            strcpy(buf, "x");
            utoa_pad((U32)qty, num, 0);
            strcat(buf, num);
            text(256 - text_width(buf, 2), y, buf, BP_INK, 2);
        } else {
            text(40, y, "ABBRECHEN", BP_INK, 2);
        }
        add_hit(8, y - 2, 256, 24, idx);
    }
    /* Bildlauf-Tasten */
    gbframe(272, 8, 40, 72, BP_PAPER);
    triangle(292, 38, 0, s_bcur > 0 ? BP_INK : BP_MID);
    add_hit(272, 8, 40, 72, HIT_UP);
    gbframe(272, 92, 40, 72, BP_PAPER);
    triangle(292, 122, 1, s_bcur < n ? BP_INK : BP_MID);
    add_hit(272, 92, 40, 72, HIT_DOWN);
    /* Beutel-Info */
    gbframe(8, 172, 196, 60, BP_PAPER);
    strcpy(buf, "");
    utoa_pad((U32)n, num, 0);
    strcat(buf, num);
    strcat(buf, "/20");
    text(20, 184, "BEUTEL", BP_INK, 1);
    text(196 - text_width(buf, 1), 184, buf, BP_INK, 1);
    utoa_pad(money_value(), num, 0);
    text(20, 206, "GELD", BP_INK, 1);
    text(196 - text_width(num, 1), 206, num, BP_INK, 1);
    back_button(212, 172, 100, 60);
}

static void draw_b_party(void)
{
    int i, count, py, hl, frame, nx;
    U16 base, hp, maxhp, stride;
    U8 species, level, st;
    char buf[24], num[8];

    count = s_bmode == BM_PARTYSUB ? wram(SYM_wPartyCount) : s_bcount;
    if (count > 6) count = 6;
    hl = (s_bmode == BM_PARTYSUB) ? wram(SYM_wWhichPokemon) : s_bcur;
    stride = (U16)(SYM_wPartyMon2 - SYM_wPartyMon1);
    frame = (g_ui.frame_counter >> 4) & 1;

    gbframe(8, 8, 304, 206, BP_PAPER);
    for (i = 0; i < count; i++) {
        py = 14 + i * 32;
        base = (U16)(SYM_wPartyMon1 + i * stride);
        species = wram(SYM_wPartySpecies + i);
        hp = wram16be((U16)(base + (SYM_wPartyMon1HP - SYM_wPartyMon1)));
        maxhp = wram16be((U16)(base + (SYM_wPartyMon1MaxHP - SYM_wPartyMon1)));
        level = wram((U16)(base + (SYM_wPartyMon1Level - SYM_wPartyMon1)));
        st = wram((U16)(base + (SYM_wPartyMon1Status - SYM_wPartyMon1)));
        if (wram(SYM_wPlayerMonNumber) == i) {
            hp = wram16be(SYM_wBattleMonHP);
            st = wram(SYM_wBattleMonStatus);
        }
        if (i == hl) {
            fill(14, py, 292, 32, BP_SEL);
            glyph(16, py + 8, 0xED, BP_INK, 2);
        }
        draw_icon(36, py, species_icon(species), (i == hl && hp) ? frame : 0, 2);
        text_game(76, py + 4, &g_gb.wram[(SYM_wPartyMonNicks + i * 11) - 0xC000], 10, BP_INK, 1);
        strcpy(buf, "Lv");
        utoa_pad(level, num, 0);
        strcat(buf, num);
        text(172, py + 4, buf, BP_INK, 1);
        status_text(st, buf);
        if (hp == 0) {
            strcpy(buf, "K.O.");
        }
        if (buf[0]) {
            nx = text_width(buf, 1);
            panel(296 - nx - 8, py + 1, nx + 8, 12, hp ? COL_ACCENT : BP_MID, BP_INK);
            text(296 - nx - 4, py + 3, buf, COL_TEXT_L, 1);
        }
        text(76, py + 19, "KP", BP_INK, 1);
        hp_bar(96, py + 18, 100, hp, maxhp);
        utoa_pad(hp, num, 3);
        strcpy(buf, num);
        strcat(buf, "/");
        utoa_pad(maxhp, num, 3);
        strcat(buf, num);
        text(298 - text_width(buf, 1), py + 19, buf, BP_INK, 1);
        if (s_bmode == BM_PARTY) {
            add_hit(8, py, 304, 32, i);
        }
    }
    if (s_bmode == BM_PARTY) {
        back_button(8, 218, 104, 20);
        text(124, 224, "W\xc3\xa4hle ein POK\xc3\xa9MON!", BP_MID, 1);
    }
}

static void draw_b_partysub(void)
{
    static const char *labels[3] = { "WECHSELN", "WERTE", "ZUR\xc3\x9c" "CK" };
    int i, y;

    draw_b_party();
    gbframe(136, 92, 176, 114, BP_PAPER);
    for (i = 0; i < 3; i++) {
        y = 100 + i * 34;
        if (i == s_bcur) {
            fill(142, y - 2, 164, 30, BP_SEL);
            cursor_glyph(146, y + 4);
        }
        text(170, y + 4, labels[i], BP_INK, 2);
        add_hit(136, y - 2, 176, 34, i);
    }
}

static void draw_battle(void)
{
    int y;
    s_nhit = 0;
    for (y = 0; y < BOT_H; y++) {
        fill(0, y, BOT_W, 1, BP_PAPER);
    }
    switch (s_bmode) {
    case BM_MAIN:
    case BM_WAIT: draw_b_main(); break;
    case BM_MOVES: draw_b_moves(); break;
    case BM_ITEMS: draw_b_items(); break;
    case BM_PARTY: draw_b_party(); break;
    default: draw_b_partysub(); break;
    }
}

/* ---- Eingabe: Tipp -> Tastenfolge ---- */

static void q_push(U8 btn)
{
    if (s_qlen < sizeof(s_qbtn)) {
        s_qbtn[s_qlen++] = btn;
    }
}

static void q_start(void)
{
    s_qlen = 0;
    s_qpos = 0;
    s_qtick = 0;
    s_qmode = s_bmode;
}

static void q_walk(int cur, int target, int wrap_count)
{
    int d;
    d = target - cur;
    if (wrap_count > 2) {
        if (d > wrap_count / 2) d -= wrap_count;
        else if (d < -(wrap_count / 2)) d += wrap_count;
    }
    while (d > 0) { q_push(BTN_DOWN); d--; }
    while (d < 0) { q_push(BTN_UP); d++; }
}

static void battle_select(int id)
{
    int cr, cc, tr, tc;
    q_start();
    if (s_bmode == BM_MAIN) {
        cr = s_bcur >> 1; cc = s_bcur & 1;
        tr = id >> 1; tc = id & 1;
        if (tr > cr) q_push(BTN_DOWN);
        if (tr < cr) q_push(BTN_UP);
        if (tc > cc) q_push(BTN_RIGHT);
        if (tc < cc) q_push(BTN_LEFT);
    } else if (s_bmode == BM_PARTY) {
        q_walk(s_bcur, id, s_bcount);
    } else {
        q_walk(s_bcur, id, 0);
    }
    q_push(BTN_A);
}

static void battle_touch(int x, int y)
{
    int i;
    if (s_qpos < s_qlen) {
        return;     /* vorherige Eingabe laeuft noch */
    }
    for (i = 0; i < s_nhit; i++) {
        BHit *r;
        r = &s_hit[i];
        if (x < r->x || y < r->y || x >= r->x + r->w || y >= r->y + r->h) {
            continue;
        }
        if (r->id == HIT_BACK) {
            q_start();
            q_push(BTN_B);
        } else if (r->id == HIT_UP) {
            q_start();
            q_push(BTN_UP);
        } else if (r->id == HIT_DOWN) {
            q_start();
            q_push(BTN_DOWN);
        } else {
            battle_select(r->id);
        }
        return;
    }
}

/* Einmal pro Frame vor gb_set_buttons() aufrufen und ODER-verknuepfen */
U8 ui_virtual_buttons(void)
{
    U8 b;
    if (s_qpos >= s_qlen) {
        return 0;
    }
    if (s_qtick == 0 && s_bmode != s_qmode) {
        s_qlen = 0;     /* Menue wurde gewechselt: Rest verwerfen */
        s_qpos = 0;
        return 0;
    }
    b = (s_qtick < 4) ? s_qbtn[s_qpos] : 0;
    s_qtick++;
    if (s_qtick >= 10) {
        s_qtick = 0;
        s_qpos++;
    }
    return b;
}

/* ---- Oberer Bildschirm: Spielmenue ausblenden ---- */

/* Zeile im Dialogkasten (Kachelzeile 14 oder 16, ab Spalte 1) */
static void put_line(int row, const U8 *s, int n)
{
    int i;
    for (i = 0; i < n && i < 18; i++) {
        g_gb.bg_override[row * 20 + 1 + i] = s[i];
    }
}

static int cat_utf8(U8 *dst, int n, const char *utf8)
{
    U8 tmp[40];
    int len, i;
    len = encode(utf8, tmp, sizeof(tmp));
    for (i = 0; i < len && n < 30; i++) {
        dst[n++] = tmp[i];
    }
    return n;
}

static int cat_nick(U8 *dst, int n, U16 addr)
{
    int i;
    U8 c;
    for (i = 0; i < 10; i++) {
        c = wram((U16)(addr + i));
        if (c == 0x50) {
            break;
        }
        dst[n++] = c;
    }
    return n;
}

/* Einmal pro Frame VOR gb_run_frame() aufrufen. Solange ein Kampfmenue offen
 * ist, zeichnet der PPU oben den gesicherten Bildschirm des Spiels
 * (wTileMapBackup: Szene mit leerem Dialogkasten, ohne Menue) und darin eine
 * Frage wie "Was soll MEW tun?". Die Menues laufen unten. */
void ui_pre_frame(void)
{
    U8 l1[32], l2[32];
    int n1, n2, i, r, c;

    g_gb.bg_override_on = 0;
    if (s_bmode == BM_NONE || s_bmode == BM_WAIT) {
        return;
    }
    /* Sicherung: nur, wenn dort wirklich Szene + Dialogkasten liegt */
    if (wram((U16)(SYM_wTileMapBackup + 12 * 20)) != 0x79) {
        return;
    }
    for (i = 0; i < 20 * 18; i++) {
        g_gb.bg_override[i] = wram((U16)(SYM_wTileMapBackup + i));
    }
    for (r = 13; r <= 16; r++) {
        for (c = 1; c <= 18; c++) {
            g_gb.bg_override[r * 20 + c] = 0x7F;
        }
    }
    n1 = 0;
    n2 = 0;
    switch (s_bmode) {
    case BM_MAIN:
    case BM_MOVES:
        n1 = cat_utf8(l1, 0, "Was soll ");
        n1 = cat_nick(l1, n1, SYM_wBattleMonNick);
        n1 = cat_utf8(l1, n1, " tun?");
        if (n1 > 18) {
            n1 = cat_utf8(l1, 0, "Was soll");
            n2 = cat_nick(l2, 0, SYM_wBattleMonNick);
            n2 = cat_utf8(l2, n2, " tun?");
        }
        break;
    case BM_ITEMS:
        n1 = cat_utf8(l1, 0, "Welches Item?");
        break;
    case BM_PARTY:
        n1 = cat_utf8(l1, 0, "W\xc3\xa4hle ein POK\xc3\xa9MON");
        n2 = cat_utf8(l2, 0, "aus!");
        break;
    default:
        n1 = cat_utf8(l1, 0, "Was tun mit");
        n2 = cat_nick(l2, 0, (U16)(SYM_wPartyMonNicks + wram(SYM_wWhichPokemon) * 11));
        n2 = cat_utf8(l2, n2, "?");
        break;
    }
    put_line(14, l1, n1);
    if (n2) {
        put_line(16, l2, n2);
    }
    g_gb.bg_override_on = 1;
}

int ui_battle_menu_open(void)
{
    return s_bmode != BM_NONE;
}

void ui_init(void)
{
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.scale_mode = SCALE_SMOOTH15;
    g_ui.color_mode = COLOR_SGB;
    g_ui.touched_slot = -1;
}

void ui_toast(const char *utf8)
{
    strncpy(g_ui.toast, utf8, sizeof(g_ui.toast) - 1);
    g_ui.toast[sizeof(g_ui.toast) - 1] = 0;
    g_ui.toast_frames = 120;
}

void ui_tick(void)
{
    g_ui.frame_counter++;
    battle_update();
    if (g_ui.toast_frames) {
        g_ui.toast_frames--;
    }
}

/* Einfache Signatur aller Werte, die der untere Bildschirm zeigt.
 * Nur bei Aenderung wird neu gezeichnet (spart auf dem alten 3DS Zeit). */
int ui_bottom_needs_redraw(void)
{
    static U32 last;
    U32 h;
    U16 a;
    h = 2166136261u;
#define MIX(v) (h = (h ^ (U32)(v)) * 16777619u)
    MIX(g_ui.tab);
    MIX(g_ui.toast_frames ? 1 + g_ui.toast[0] : 0);
    MIX(g_ui.frame_counter >> 4);
    MIX(g_ui.touched_slot);
    MIX(g_ui.scale_mode);
    MIX(g_ui.save_state);
    MIX(g_ui.audio_state);
    MIX(g_gb.color_mode);
    for (a = SYM_wPartyCount; a < SYM_wPartyMonNicks + 6 * 11; a++) {
        MIX(wram(a));
    }
    MIX(wram(SYM_wCurMap));
    MIX(wram(SYM_wIsInBattle));
    MIX(wram16be(SYM_wBattleMonHP));
    MIX(wram(SYM_wPlayerMonNumber));
    if (g_ui.tab == TAB_OPTIONS) {
        MIX(wram(SYM_wObtainedBadges));
        MIX(wram(SYM_wPlayerMoney));
        MIX(wram(SYM_wPlayerMoney + 1));
        MIX(wram(SYM_wPlayerMoney + 2));
        MIX(wram(SYM_wPlayTimeMinutes));
        MIX(wram(SYM_wPlayTimeHours));
        MIX(wram(SYM_wPlayerName));
    }
    h = battle_signature(h);
#undef MIX
    if (h == last) {
        return 0;
    }
    last = h;
    return 1;
}

void ui_draw_bottom(U16 *fb)
{
    target(fb, BOT_W, BOT_H);
    if (s_bmode != BM_NONE) {
        draw_battle();
        return;
    }
    draw_background();
    switch (g_ui.tab) {
    case TAB_MAP: draw_map(); break;
    case TAB_OPTIONS: draw_options(); break;
    default: draw_team(); break;
    }
    draw_tabs();
    if (g_ui.toast_frames) {
        panel(60, BOT_H - 30, 200, 22, COL_PANEL_SEL, COL_BORDER);
        text_center(160, BOT_H - 23, g_ui.toast, COL_TEXT, 1);
    }
}

static int hit(const Rect *r, int x, int y)
{
    return x >= r->x && y >= r->y && x < r->x + r->w && y < r->y + r->h;
}

void ui_touch(int x, int y)
{
    int i;
    if (s_bmode != BM_NONE) {
        battle_touch(x, y);
        return;
    }
    if (y < TAB_H) {
        g_ui.tab = (U8)(x / (BOT_W / TAB_COUNT));
        if (g_ui.tab >= TAB_COUNT) g_ui.tab = TAB_COUNT - 1;
        return;
    }
    if (g_ui.tab == TAB_OPTIONS) {
        for (i = 0; i < 3; i++) {
            if (hit(&s_btn_color[i], x, y)) {
                g_ui.color_mode = (U8)i;
                gb_set_color_mode((U8)i);
                g_ui.settings_changed = 1;
            }
        }
        for (i = 0; i < SCALE_COUNT; i++) {
            if (hit(&s_btn_scale[i], x, y)) {
                g_ui.scale_mode = (U8)i;
                g_ui.settings_changed = 1;
            }
        }
    } else if (g_ui.tab == TAB_TEAM) {
        int col, row;
        col = (x - 6) / 156;
        row = (y - TAB_H - 6) / 70;
        if (col >= 0 && col < 2 && row >= 0 && row < 3) {
            g_ui.touched_slot = (S8)(row * 2 + col);
        }
    }
}

void ui_next_tab(int dir)
{
    g_ui.tab = (U8)((g_ui.tab + TAB_COUNT + dir) % TAB_COUNT);
}

void ui_settings_save(U8 *out8)
{
    memset(out8, 0, 8);
    out8[0] = 'P';
    out8[1] = '3';
    out8[2] = g_ui.scale_mode;
    out8[3] = g_ui.color_mode;
    out8[4] = g_ui.tab;
}

void ui_settings_load(const U8 *in8)
{
    if (in8[0] != 'P' || in8[1] != '3') {
        return;
    }
    if (in8[2] < SCALE_COUNT) g_ui.scale_mode = in8[2];
    if (in8[3] <= COLOR_SGB) g_ui.color_mode = in8[3];
    if (in8[4] < TAB_COUNT) g_ui.tab = in8[4];
    gb_set_color_mode(g_ui.color_mode);
}
