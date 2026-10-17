/*
 * main.c - 3DS-Frontend fuer den nativen Port von pokered-de.
 *
 * Oben:  Spielbild (Overworld, Kaempfe, Menues des Spiels)
 * Unten: Team / Karte / Optionen (Touch, L/R wechseln die Seite)
 *
 * Speicherstand: als CIA in /sav.dat im Spielstand-Archiv des Titels
 * (0004000000171300) - dasselbe Format wie die Virtual-Console-Version,
 * damit PKSM und die Pokemon Kiste ihn direkt lesen koennen. Als .3dsx
 * (Homebrew Launcher) landet er stattdessen auf der SD-Karte.
 */
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/stat.h>

#include "gb.h"
#include "ui.h"
#include "pokered_bin.h"

#define SD_DIR      "sdmc:/3ds/pokered-de"
#define SD_SAVE     SD_DIR "/sav.dat"
#define SD_CFG      SD_DIR "/port3ds.cfg"
#define SD_LOG      SD_DIR "/log.txt"
#define SRAM_SIZE   0x8000

/* ------------------------------------------------------------------ */
/* Logging                                                             */
/* ------------------------------------------------------------------ */

void gb_log(const char *fmt, ...)
{
    va_list ap;
    FILE *lf;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    /* Logdatei auf der SD-Karte (zur Fehlersuche) */
    mkdir("sdmc:/3ds", 0777);
    mkdir(SD_DIR, 0777);
    lf = fopen(SD_LOG, "a");
    if (lf) {
        va_start(ap, fmt);
        vfprintf(lf, fmt, ap);
        va_end(ap);
        fclose(lf);
    }
}

/* ------------------------------------------------------------------ */
/* Speichern: Spielstand-Archiv (CIA) oder SD (3dsx)                   */
/* ------------------------------------------------------------------ */

static int s_use_archive;
static volatile int s_archive_bad;   /* Archiv nicht nutzbar -> nur noch SD */

static Result archive_rw(const char *path, void *data, u32 size, int write)
{
    FS_Archive arch;
    Handle fh;
    Result rc;
    u32 n;
    u64 fsize;
    FS_Path fpath;

    rc = FSUSER_OpenArchive(&arch, ARCHIVE_SAVEDATA, fsMakePath(PATH_EMPTY, ""));
    if (R_FAILED(rc) && (rc == (Result)0xC8A04554 || rc == (Result)0xC8A04555)) {
        /* Spielstand-Archiv existiert, ist aber noch nicht formatiert (erster
         * Start nach der Installation). Wie Retail-Spiele selbst formatieren:
         * Daten intern doppelt gespeichert (wie bei Retail-Spielen). */
        gb_log("Spielstand-Archiv nicht formatiert (%08lX) -> formatieren\n", (unsigned long)rc);
        /* Parameter 3 ist die Blockgroesse (0x200 oder 0x1000), nicht die Anzahl;
         * die Gesamtgroesse kommt aus dem Exheader (SaveDataSize). */
        rc = FSUSER_FormatSaveData(ARCHIVE_SAVEDATA, fsMakePath(PATH_EMPTY, ""), 0x200, 10, 10, 11, 11, true);
        gb_log("FormatSaveData: %08lX\n", (unsigned long)rc);
        if (R_SUCCEEDED(rc)) {
            rc = FSUSER_OpenArchive(&arch, ARCHIVE_SAVEDATA, fsMakePath(PATH_EMPTY, ""));
        }
    }
    if (R_FAILED(rc)) {
        gb_log("OpenArchive: %08lX -> Archiv wird nicht mehr verwendet\n", (unsigned long)rc);
        s_archive_bad = 1;
        return rc;
    }
    fpath = fsMakePath(PATH_ASCII, path);
    if (write) {
        /* Im Spielstand-Archiv haben Dateien feste Groessen: vorhandene Datei
         * mit falscher Groesse loeschen, fehlende mit Groesse neu anlegen. */
        rc = FSUSER_OpenFile(&fh, arch, fpath, FS_OPEN_WRITE, 0);
        if (R_SUCCEEDED(rc)) {
            fsize = 0;
            FSFILE_GetSize(fh, &fsize);
            if (fsize != (u64)size) {
                FSFILE_Close(fh);
                FSUSER_DeleteFile(arch, fpath);
                rc = -1;
            }
        }
        if (R_FAILED(rc)) {
            rc = FSUSER_CreateFile(arch, fpath, 0, (u64)size);
            if (R_FAILED(rc)) {
                gb_log("CreateFile %s: %08lX\n", path, (unsigned long)rc);
            } else {
                rc = FSUSER_OpenFile(&fh, arch, fpath, FS_OPEN_WRITE, 0);
            }
        }
        if (R_SUCCEEDED(rc)) {
            rc = FSFILE_Write(fh, &n, 0, data, size, FS_WRITE_FLUSH);
            FSFILE_Close(fh);
            if (R_FAILED(rc)) {
                gb_log("Write %s: %08lX\n", path, (unsigned long)rc);
            } else {
                rc = FSUSER_ControlArchive(arch, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
                if (R_FAILED(rc)) {
                    gb_log("Commit: %08lX\n", (unsigned long)rc);
                }
            }
        } else {
            gb_log("OpenFile %s: %08lX\n", path, (unsigned long)rc);
        }
    } else {
        rc = FSUSER_OpenFile(&fh, arch, fpath, FS_OPEN_READ, 0);
        if (R_SUCCEEDED(rc)) {
            n = 0;
            rc = FSFILE_Read(fh, &n, 0, data, size);
            FSFILE_Close(fh);
            if (R_SUCCEEDED(rc) && n != size) {
                rc = -1;
            }
        }
    }
    FSUSER_CloseArchive(arch);
    return rc;
}

static int sd_rw(const char *path, void *data, u32 size, int write)
{
    FILE *f;
    size_t n;
    mkdir("sdmc:/3ds", 0777);
    mkdir(SD_DIR, 0777);
    f = fopen(path, write ? "wb" : "rb");
    if (!f) {
        return -1;
    }
    n = write ? fwrite(data, 1, size, f) : fread(data, 1, size, f);
    fclose(f);
    return n == size ? 0 : -1;
}

static int store_rw(const char *arch_path, const char *sd_path, void *data, u32 size, int write)
{
    if (s_use_archive && !s_archive_bad) {
        if (R_SUCCEEDED(archive_rw(arch_path, data, size, write))) {
            return 0;
        }
        /* Archiv nicht nutzbar: Spielstand zumindest auf der SD-Karte sichern/laden */
        if (sd_rw(sd_path, data, size, write) == 0) {
            return 1;
        }
        return -1;
    }
    return sd_rw(sd_path, data, size, write);
}

/* Speichern im Hintergrund: der Archiv-Zugriff dauert (Commit, SD) und darf
 * weder Bild noch Ton anhalten. Der Hauptthread kopiert den SRAM und stoesst
 * den Worker an; Ergebnisse werden im Hauptthread ausgewertet. */
#define JOB_SRAM 1
#define JOB_CFG  2

static Thread s_thread;
static LightEvent s_ev;
static volatile int s_quit;
static volatile u32 s_jobs;
static volatile int s_busy;
static volatile int s_done_sram;
static volatile int s_sram_rc;
static int s_want_sram, s_want_cfg, s_force_save;
static U8 s_snap[SRAM_SIZE];
static U8 s_last_saved[SRAM_SIZE];
static U8 s_cfg_snap[8];

static void run_jobs(u32 jobs)
{
    int rc;

    if (jobs & JOB_SRAM) {
        rc = store_rw("/sav.dat", SD_SAVE, s_snap, SRAM_SIZE, 1);
        s_sram_rc = rc;
        if (rc >= 0) {
            memcpy(s_last_saved, s_snap, SRAM_SIZE);
        }
        s_done_sram = 1;
    }
    if (jobs & JOB_CFG) {
        store_rw("/port3ds.cfg", SD_CFG, s_cfg_snap, sizeof(s_cfg_snap), 1);
    }
}

static void save_worker(void *arg)
{
    u32 jobs;

    (void)arg;
    for (;;) {
        LightEvent_Wait(&s_ev);
        if (s_quit) {
            break;
        }
        jobs = s_jobs;
        s_jobs = 0;
        run_jobs(jobs);
        __atomic_store_n(&s_busy, 0, __ATOMIC_RELEASE);
    }
}

static void save_start(void)
{
    s32 prio;

    LightEvent_Init(&s_ev, RESET_ONESHOT);
    prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio += 1;
    if (prio > 0x3F) {
        prio = 0x3F;
    }
    s_thread = threadCreate(save_worker, NULL, 32 * 1024, prio, -1, false);
    if (!s_thread) {
        gb_log("Speicher-Thread fehlgeschlagen -> synchron speichern\n");
    }
}

/* Einmal pro Frame im Hauptthread */
static void save_pump(void)
{
    u32 jobs;
    int rc;

    if (__atomic_load_n(&s_busy, __ATOMIC_ACQUIRE)) {
        return;
    }
    if (s_done_sram) {
        s_done_sram = 0;
        rc = s_sram_rc;
        if (rc == 0) {
            g_ui.save_state = s_use_archive ? 1 : 2;
            ui_toast("Spielstand gesichert");
        } else if (rc > 0) {
            g_ui.save_state = 2;
            ui_toast("Gesichert (nur SD-Karte)");
        } else {
            g_ui.save_state = 3;
            ui_toast("Fehler beim Sichern!");
            g_gb.sram_dirty |= 1;     /* spaeter erneut versuchen */
        }
    }
    jobs = 0;
    if (s_want_sram) {
        s_want_sram = 0;
        memcpy(s_snap, g_gb.sram, SRAM_SIZE);
        g_gb.sram_dirty &= (U8)~1;
        if (s_force_save || memcmp(s_snap, s_last_saved, SRAM_SIZE) != 0) {
            s_force_save = 0;
            jobs |= JOB_SRAM;
        }
    }
    if (s_want_cfg) {
        s_want_cfg = 0;
        ui_settings_save(s_cfg_snap);
        jobs |= JOB_CFG;
    }
    if (!jobs) {
        return;
    }
    if (s_thread) {
        s_busy = 1;
        s_jobs = jobs;
        LightEvent_Signal(&s_ev);
    } else {
        run_jobs(jobs);
    }
}

/* Beim Beenden: Worker anhalten und alles Ausstehende synchron schreiben */
static void save_finish(void)
{
    if (s_thread) {
        s_quit = 1;
        LightEvent_Signal(&s_ev);
        threadJoin(s_thread, U64_MAX);
        threadFree(s_thread);
        s_thread = NULL;
    }
    if ((g_gb.sram_dirty & 1) || s_want_sram) {
        s_want_sram = 1;
    }
    s_want_cfg = 1;
    save_pump();
}

/* ------------------------------------------------------------------ */
/* Bildausgabe: zeilenweiser RGB565-Puffer -> gedrehter Framebuffer    */
/* ------------------------------------------------------------------ */

static U16 s_top[TOP_W * TOP_H];
static U16 s_bot[BOT_W * BOT_H];

static void present(gfxScreen_t screen, const U16 *src, int w, int h)
{
    u16 *fb;
    u16 fw, fh;
    int x, y;

    fb = (u16 *)gfxGetFramebuffer(screen, GFX_LEFT, &fw, &fh);
    /* Framebuffer ist 240 hoch (fw) x Breite (fh), spaltenweise, von unten */
    for (x = 0; x < w; x++) {
        u16 *col;
        col = &fb[x * h];
        for (y = 0; y < h; y++) {
            col[h - 1 - y] = src[y * w + x];
        }
    }
    (void)fw;
    (void)fh;
}

/* ------------------------------------------------------------------ */
/* Audio (NDSP)                                                        */
/* ------------------------------------------------------------------ */

#define AUDIO_BUFS        8
#define AUDIO_BUF_FRAMES  1024

static int s_audio_ok;
static ndspWaveBuf s_wave[AUDIO_BUFS];
static short *s_audio_mem;
static short s_audio_tmp[AUDIO_BUF_FRAMES * 2];

static void audio_init(void)
{
    float mix[12];
    int i;
    Result rc;
    MemInfo mi;
    PageInfo pi;

    /* ndspInit schreibt in den DSP-Speicher (0x1FF40000-0x1FF7FFFF). Ist er
     * nicht eingeblendet (Exheader ohne IORegisterMapping), wuerde das
     * Programm abstuerzen -> vorher pruefen und sonst ohne Ton starten. */
    if (R_FAILED(svcQueryMemory(&mi, &pi, 0x1FF50000)) || mi.state == MEMSTATE_FREE
        || R_FAILED(svcQueryMemory(&mi, &pi, 0x1FF70000)) || mi.state == MEMSTATE_FREE) {
        s_audio_ok = 0;
        g_ui.audio_state = 2;
        gb_log("DSP-Speicher nicht eingeblendet -> ohne Ton\n");
        return;
    }
    gb_log("DSP-Speicher: state %lu perm %lu\n", (unsigned long)mi.state, (unsigned long)mi.perm);
    rc = ndspInit();
    if (R_FAILED(rc)) {
        s_audio_ok = 0;   /* dspfirm.cdc fehlt: ohne Ton weiterspielen */
        g_ui.audio_state = 2;
        g_ui.audio_rc = (U32)rc;
        gb_log("ndspInit: %08lX (sdmc:/3ds/dspfirm.cdc vorhanden?)\n", (unsigned long)rc);
        return;
    }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, (float)APU_RATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
    memset(mix, 0, sizeof(mix));
    mix[0] = 1.0f;
    mix[1] = 1.0f;
    ndspChnSetMix(0, mix);
    s_audio_mem = (short *)linearAlloc(AUDIO_BUFS * AUDIO_BUF_FRAMES * 4);
    if (!s_audio_mem) {
        ndspExit();
        s_audio_ok = 0;
        g_ui.audio_state = 2;
        g_ui.audio_rc = 0xFFFFFFFFu;
        gb_log("linearAlloc fuer Audio fehlgeschlagen\n");
        return;
    }
    memset(s_wave, 0, sizeof(s_wave));
    for (i = 0; i < AUDIO_BUFS; i++) {
        s_wave[i].data_vaddr = &s_audio_mem[i * AUDIO_BUF_FRAMES * 2];
        s_wave[i].status = NDSP_WBUF_DONE;
    }
    s_audio_ok = 1;
    g_ui.audio_state = 1;
}

static void audio_frame(void)
{
    u32 n;
    int i, queued;

    n = gb_audio_read(s_audio_tmp, AUDIO_BUF_FRAMES);
    if (!s_audio_ok || n == 0) {
        return;
    }
    queued = 0;
    for (i = 0; i < AUDIO_BUFS; i++) {
        if (s_wave[i].status == NDSP_WBUF_QUEUED || s_wave[i].status == NDSP_WBUF_PLAYING) {
            queued++;
        }
    }
    /* Zu viel gepuffert (Spiel laeuft minimal schneller als 32768 Hz)? -> verwerfen */
    if (queued >= AUDIO_BUFS - 1) {
        return;
    }
    for (i = 0; i < AUDIO_BUFS; i++) {
        if (s_wave[i].status == NDSP_WBUF_DONE || s_wave[i].status == NDSP_WBUF_FREE) {
            memcpy((void *)s_wave[i].data_vaddr, s_audio_tmp, n * 4);
            s_wave[i].nsamples = n;
            DSP_FlushDataCache(s_wave[i].data_vaddr, n * 4);
            ndspChnWaveBufAdd(0, &s_wave[i]);
            break;
        }
    }
}

static void audio_exit(void)
{
    if (s_audio_ok) {
        ndspChnReset(0);
        linearFree(s_audio_mem);
        ndspExit();
    }
}

/* ------------------------------------------------------------------ */
/* Eingabe                                                             */
/* ------------------------------------------------------------------ */

static U8 map_buttons(u32 held)
{
    U8 b;
    b = 0;
    if (held & KEY_A) b |= BTN_A;
    if (held & (KEY_B | KEY_Y)) b |= BTN_B;
    if (held & KEY_START) b |= BTN_START;
    if (held & KEY_SELECT) b |= BTN_SELECT;
    if (held & (KEY_DUP | KEY_CPAD_UP)) b |= BTN_UP;
    if (held & (KEY_DDOWN | KEY_CPAD_DOWN)) b |= BTN_DOWN;
    if (held & (KEY_DLEFT | KEY_CPAD_LEFT)) b |= BTN_LEFT;
    if (held & (KEY_DRIGHT | KEY_CPAD_RIGHT)) b |= BTN_RIGHT;
    /* gegenueberliegende Richtungen nie gleichzeitig */
    if ((b & (BTN_UP | BTN_DOWN)) == (BTN_UP | BTN_DOWN)) b &= (U8)~BTN_DOWN;
    if ((b & (BTN_LEFT | BTN_RIGHT)) == (BTN_LEFT | BTN_RIGHT)) b &= (U8)~BTN_RIGHT;
    return b;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    U8 cfg[8];
    u32 down, held;
    int save_delay, rc, logged_frames;
    touchPosition touch;

    (void)argc;
    (void)argv;

    osSetSpeedupEnable(true);
    gfxInitDefault();
    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
    gfxSetScreenFormat(GFX_BOTTOM, GSP_RGB565_OES);
    gfxSetDoubleBuffering(GFX_TOP, true);
    /* unten nur bei Aenderungen zeichnen -> einfacher Puffer */
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    gfxSet3D(false);
    fsInit();
    ui_init();
    s_use_archive = !envIsHomebrew();
    gb_log("--- Start, Build " __DATE__ " " __TIME__ " (%s)\n", s_use_archive ? "CIA, Spielstand-Archiv" : "3dsx, SD-Karte");

    /* Abgesicherter Start: L gedrueckt halten -> ohne Ton,
     * R gedrueckt halten -> gespeicherte Einstellungen ignorieren */
    hidScanInput();
    held = hidKeysHeld();
    if (held & KEY_L) {
        gb_log("Ton per L uebersprungen\n");
    } else {
        gb_log("ndspInit ...\n");
        audio_init();
        gb_log("Ton: %s\n", s_audio_ok ? "an" : "aus");
    }

    gb_log("gb_init (ROM %lu Bytes)\n", (unsigned long)pokered_bin_size);
    gb_init(pokered_bin, (U32)pokered_bin_size);
    rc = store_rw("/sav.dat", SD_SAVE, g_gb.sram, SRAM_SIZE, 0);
    if (rc < 0) {
        memset(g_gb.sram, 0xFF, SRAM_SIZE);
    }
    gb_log("Spielstand: %s\n", rc == 0 ? "geladen" : rc > 0 ? "von SD geladen" : "keiner");
    g_gb.sram_dirty = 0;
    memcpy(s_last_saved, g_gb.sram, SRAM_SIZE);
    if (rc > 0 && s_use_archive && !s_archive_bad) {
        /* Stand lag nur auf der SD-Karte (frueherer Fallback): ins Archiv uebernehmen */
        s_force_save = 1;
        s_want_sram = 1;
    }
    save_start();
    if (!(held & KEY_R) && store_rw("/port3ds.cfg", SD_CFG, cfg, sizeof(cfg), 0) >= 0) {
        gb_log("Einstellungen: %02X %02X %02X %02X %02X\n", cfg[0], cfg[1], cfg[2], cfg[3], cfg[4]);
        ui_settings_load(cfg);
    }
    gb_set_color_mode(g_ui.color_mode);
    gb_log("Hauptschleife\n");
    logged_frames = 0;

    save_delay = 0;
    while (aptMainLoop()) {
        hidScanInput();
        down = hidKeysDown();
        held = hidKeysHeld();

        if (down & KEY_L) ui_next_tab(-1);
        if (down & KEY_R) ui_next_tab(1);
        if (down & KEY_TOUCH) {
            hidTouchRead(&touch);
            ui_touch(touch.px, touch.py);
        }
        /* START + SELECT + L + R: zurueck ins HOME-Menue */
        if ((held & (KEY_START | KEY_SELECT | KEY_L | KEY_R)) == (KEY_START | KEY_SELECT | KEY_L | KEY_R)) {
            break;
        }

        gb_set_buttons(map_buttons(held));
        gb_run_frame();
        audio_frame();

        /* Spiel hat SRAM nach dem Schreiben gesperrt -> kurz danach sichern */
        if (g_gb.sram_dirty & 2) {
            if (g_gb.sram_dirty & 1) {
                save_delay = 30;
            }
            g_gb.sram_dirty &= (U8)~2;
        }
        if (save_delay > 0 && --save_delay == 0) {
            s_want_sram = 1;
        }
        if (g_ui.settings_changed) {
            g_ui.settings_changed = 0;
            s_want_cfg = 1;
        }
        save_pump();

        if (logged_frames < 2) {
            logged_frames++;
            gb_log("Frame %d ok\n", logged_frames);
        }
        ui_tick();
        ui_draw_top(s_top);
        present(GFX_TOP, s_top, TOP_W, TOP_H);
        if (ui_bottom_needs_redraw()) {
            ui_draw_bottom(s_bot);
            present(GFX_BOTTOM, s_bot, BOT_W, BOT_H);
        }
        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
        if (logged_frames == 2) {
            logged_frames++;
            gb_log("Erstes Bild ausgegeben\n");
        }
    }

    save_finish();
    audio_exit();
    fsExit();
    gfxExit();
    return 0;
}
