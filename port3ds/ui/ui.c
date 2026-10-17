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
