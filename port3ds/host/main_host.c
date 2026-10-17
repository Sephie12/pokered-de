/*
 * main_host.c - Headless-Testfrontend fuer Linux.
 *
 * Aufruf: pokered_host <rom.gbc> [optionen]
 *   -f N        Anzahl Frames (Standard 600)
 *   -i SCRIPT   Eingabeskript: Zeilen "FRAME TASTEN" (z.B. "300 A", "310 -")
 *   -s N        alle N Frames Screenshot (PPM) nach shots/
 *   -m MODUS    dmg | gbc | sgb
 *   -I          nur Interpreter (Differenztest-Referenz)
 *   -t FILE     Trace: pro Frame Hash von WRAM/VRAM/Regs
 *   -w FILE     Audio als WAV schreiben
 *   -S FILE     SRAM laden/speichern (sav.dat)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../core/gb.h"
#include "../ui/ui.h"

void gb_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static U8 *s_miss;      /* 512 Baenke * 32 KiB Bitfeld */
static void miss_hook(U16 bank, U16 pc)
{
    U32 i;
    i = ((U32)bank << 15) | pc;
    s_miss[i] = 1;
}

static U8 parse_buttons(const char *s)
{
    U8 b;
    b = 0;
    for (; *s; s++) {
        switch (*s) {
        case 'A': b |= BTN_A; break;
        case 'B': b |= BTN_B; break;
        case 'S': b |= BTN_START; break;
        case 's': b |= BTN_SELECT; break;
        case 'U': b |= BTN_UP; break;
        case 'D': b |= BTN_DOWN; break;
        case 'L': b |= BTN_LEFT; break;
        case 'R': b |= BTN_RIGHT; break;
        default: break;
        }
    }
    return b;
}

static void write_ppm(const char *path)
{
    FILE *f;
    int i;
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", GB_W, GB_H);
    for (i = 0; i < GB_W * GB_H; i++) {
        U16 c;
        U8 rgb[3];
        c = g_gb.frame[i];
        rgb[0] = (U8)(((c >> 11) & 0x1F) * 255 / 31);
        rgb[1] = (U8)(((c >> 5) & 0x3F) * 255 / 63);
        rgb[2] = (U8)((c & 0x1F) * 255 / 31);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void write_ppm_buf(const char *path, const U16 *buf, int w, int h)
{
    FILE *f;
    int i;
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (i = 0; i < w * h; i++) {
        U8 rgb[3];
        rgb[0] = (U8)(((buf[i] >> 11) & 0x1F) * 255 / 31);
        rgb[1] = (U8)(((buf[i] >> 5) & 0x3F) * 255 / 63);
        rgb[2] = (U8)((buf[i] & 0x1F) * 255 / 31);
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static U32 fnv(const U8 *p, U32 n, U32 h)
{
    U32 i;
    for (i = 0; i < n; i++) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

static void wr32(FILE *f, U32 v) { fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f); fputc((v >> 16) & 0xFF, f); fputc(v >> 24, f); }
static void wr16(FILE *f, U16 v) { fputc(v & 0xFF, f); fputc(v >> 8, f); }

int main(int argc, char **argv)
{
    FILE *f;
    U8 *rom;
    long size;
    int frames, shot_every, i, interp_only, frame;
    const char *script, *trace_path, *wav_path, *sav_path, *miss_path;
    U8 mode;
    FILE *trace, *wav;
    U32 wav_samples;
    static short abuf[4096 * 2];
    int next_line_frame;
    char line_btn[64];
    FILE *sf;
    U8 cur_buttons;

    if (argc < 2) {
        fprintf(stderr, "Aufruf: %s rom.gbc [-f N] [-i script] [-s N] [-m dmg|gbc|sgb] [-I] [-t trace] [-w wav] [-S sav]\n", argv[0]);
        return 1;
    }
    frames = 600;
    shot_every = 0;
    script = NULL;
    mode = COLOR_SGB;
    interp_only = 0;
    trace_path = NULL;
    wav_path = NULL;
    sav_path = NULL;
    miss_path = NULL;
    for (i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-f") && i + 1 < argc) frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) shot_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-m") && i + 1 < argc) {
            i++;
            mode = !strcmp(argv[i], "dmg") ? COLOR_DMG : !strcmp(argv[i], "gbc") ? COLOR_GBC : COLOR_SGB;
        }
        else if (!strcmp(argv[i], "-I")) interp_only = 1;
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) trace_path = argv[++i];
        else if (!strcmp(argv[i], "-w") && i + 1 < argc) wav_path = argv[++i];
        else if (!strcmp(argv[i], "-S") && i + 1 < argc) sav_path = argv[++i];
        else if (!strcmp(argv[i], "-M") && i + 1 < argc) miss_path = argv[++i];
    }

    f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    rom = (U8 *)malloc((size_t)size);
    if (fread(rom, 1, (size_t)size, f) != (size_t)size) { fclose(f); return 1; }
    fclose(f);

    gb_init(rom, (U32)size);
    gb_set_color_mode(mode);
    gb_set_recompiled(!interp_only);
    if (sav_path) {
        f = fopen(sav_path, "rb");
        if (f) {
            if (fread(g_gb.sram, 1, sizeof(g_gb.sram), f) != sizeof(g_gb.sram)) {
                fprintf(stderr, "Warnung: sav.dat unvollstaendig\n");
            }
            fclose(f);
        }
    }

    if (miss_path) {
        s_miss = (U8 *)calloc(512u << 15, 1);
        gb_miss_hook = miss_hook;
    }
    trace = trace_path ? fopen(trace_path, "w") : NULL;
    wav = wav_path ? fopen(wav_path, "wb") : NULL;
    wav_samples = 0;
    if (wav) {
        fwrite("RIFF\0\0\0\0WAVEfmt ", 1, 16, wav);
        wr32(wav, 16); wr16(wav, 1); wr16(wav, 2); wr32(wav, APU_RATE); wr32(wav, APU_RATE * 4); wr16(wav, 4); wr16(wav, 16);
        fwrite("data\0\0\0\0", 1, 8, wav);
    }

    sf = script ? fopen(script, "r") : NULL;
    next_line_frame = -1;
    line_btn[0] = 0;
    if (sf && fscanf(sf, "%d %63s", &next_line_frame, line_btn) != 2) next_line_frame = -1;
    cur_buttons = 0;

    for (frame = 0; frame < frames; frame++) {
        while (next_line_frame >= 0 && frame >= next_line_frame) {
            cur_buttons = parse_buttons(line_btn);
            if (fscanf(sf, "%d %63s", &next_line_frame, line_btn) != 2) next_line_frame = -1;
        }
        gb_set_buttons(cur_buttons);
        gb_run_frame();
        if (wav) {
            U32 n;
            n = gb_audio_read(abuf, 4096);
            fwrite(abuf, 4, n, wav);
            wav_samples += n;
        } else {
            (void)gb_audio_read(abuf, 4096);
        }
        if (trace) {
            U32 h;
            h = fnv(g_gb.wram, sizeof(g_gb.wram), 2166136261u);
            h = fnv(g_gb.hram, sizeof(g_gb.hram), h);
            fprintf(trace, "%d wram/hram=%08x vram=%08x oam=%08x pc=%04x bank=%d\n", frame, h,
                    fnv(g_gb.vram, sizeof(g_gb.vram), 2166136261u),
                    fnv(g_gb.oam, sizeof(g_gb.oam), 2166136261u),
                    g_gb.cpu.pc, g_gb.rom_bank);
        }
        if (shot_every && (frame % shot_every) == shot_every - 1) {
            char path[64];
            sprintf(path, "shots/f%05d.ppm", frame + 1);
            write_ppm(path);
        }
    }
    if (wav) {
        fseek(wav, 4, SEEK_SET); wr32(wav, 36 + wav_samples * 4);
        fseek(wav, 40, SEEK_SET); wr32(wav, wav_samples * 4);
        fclose(wav);
    }
    if (trace) fclose(trace);
    if (sav_path && g_gb.sram_dirty) {
        f = fopen(sav_path, "wb");
        if (f) { fwrite(g_gb.sram, 1, sizeof(g_gb.sram), f); fclose(f); }
    }
    if (miss_path) {
        U32 k, n;
        f = fopen(miss_path, "a");
        n = 0;
        for (k = 0; k < (512u << 15); k++) {
            if (s_miss[k]) {
                fprintf(f, "%02x:%04x\n", k >> 15, k & 0x7FFF);
                n++;
            }
        }
        fclose(f);
        fprintf(stderr, "Misses: %u Adressen\n", n);
    }
    fprintf(stderr, "Frames: %d  Interpreter-Schritte: %u  Dispatches: %u\n",
            frames, g_gb.interp_steps, g_gb.dispatch_count);
    write_ppm("shots/last.ppm");
    {
        static U16 top[TOP_W * TOP_H], bot[BOT_W * BOT_H];
        int t, sc;
        ui_init();
        g_ui.color_mode = mode;
        for (sc = 0; sc < SCALE_COUNT; sc++) {
            char p[64];
            g_ui.scale_mode = (U8)sc;
            ui_draw_top(top);
            sprintf(p, "shots/top%d.ppm", sc);
            write_ppm_buf(p, top, TOP_W, TOP_H);
        }
        for (t = 0; t < TAB_COUNT; t++) {
            char p[64];
            g_ui.tab = (U8)t;
            g_ui.frame_counter = 0;
            ui_tick();
            (void)ui_bottom_needs_redraw();
            ui_draw_bottom(bot);
            sprintf(p, "shots/bottom%d.ppm", t);
            write_ppm_buf(p, bot, BOT_W, BOT_H);
        }
    }
    return 0;
}
