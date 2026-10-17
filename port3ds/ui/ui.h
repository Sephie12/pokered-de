/*
 * ui.h - plattformunabhaengige Bildschirm-Zusammensetzung.
 *
 * Oberer Bildschirm (400x240): Spielbild, skaliert oder 1:1 mit SGB-Rahmen.
 * Unterer Bildschirm (320x240): Team, Karte, Optionen - im Stil der
 * DS/3DS-Pokemon-Spiele. Alle Puffer sind zeilenweise RGB565; das
 * 3DS-Frontend dreht sie fuer den Framebuffer.
 */
#ifndef UI_H
#define UI_H

#include "../core/gb.h"

#define TOP_W 400
#define TOP_H 240
#define BOT_W 320
#define BOT_H 240

#define SCALE_SMOOTH15 0   /* 1,5x mit weicher Zwischenzeile */
#define SCALE_BORDER   1   /* 1:1 mit SGB-Rahmen */
#define SCALE_SHARP15  2   /* 1,5x Pixelwiederholung (scharf) */
#define SCALE_COUNT    3

#define TAB_TEAM    0
#define TAB_MAP     1
#define TAB_OPTIONS 2
#define TAB_COUNT   3

typedef struct {
    U8  scale_mode;
    U8  color_mode;
    U8  tab;
    U8  settings_changed;
    U16 toast_frames;
    char toast[40];
    U32 frame_counter;
    S8  touched_slot;
    U8  audio_state;    /* 0 unbekannt, 1 an, 2 aus (Firmware fehlt) */
    U8  save_state;     /* 0 noch nicht, 1 Archiv, 2 SD-Karte, 3 Fehler */
    U32 audio_rc;
} UiState;

extern UiState g_ui;

void ui_init(void);
void ui_draw_top(U16 *fb);
void ui_draw_bottom(U16 *fb);
void ui_tick(void);                     /* einmal pro Frame */
int  ui_bottom_needs_redraw(void);      /* 1, wenn sich etwas geaendert hat */
void ui_touch(int x, int y);            /* bei neuem Touch-Druck */
void ui_next_tab(int dir);
void ui_toast(const char *utf8);

/* Einstellungen als 8 Bytes (fuer die Konfigurationsdatei) */
void ui_settings_save(U8 *out8);
void ui_settings_load(const U8 *in8);

#endif
