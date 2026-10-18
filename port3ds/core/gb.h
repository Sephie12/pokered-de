/*
 * gb.h - Laufzeitkern fuer den nativen 3DS-Port von pokered-de.
 *
 * Der Spielcode laeuft als statisch rekompiliertes C (siehe recomp/),
 * dieser Kern stellt CPU-Zustand, Speicher, MBC5, PPU, APU, Timer,
 * Serial und SGB-Paketempfang bereit. Ein SM83-Interpreter dient als
 * Fallback fuer Code ausserhalb des ROMs (z.B. die DMA-Routine im HRAM)
 * und als Referenz fuer Differenztests.
 *
 * Stil: Deklarationen immer am Blockanfang (C89-kompatibel).
 */
#ifndef GB_CORE_H_INCLUDED
#define GB_CORE_H_INCLUDED

#include <stddef.h>

typedef unsigned char  U8;
typedef signed char    S8;
typedef unsigned short U16;
typedef unsigned int   U32;
typedef signed int     S32;

#if defined(__GNUC__)
#define GB_INLINE static __inline__ __attribute__((always_inline))
#define GB_LIKELY(x)   __builtin_expect(!!(x), 1)
#define GB_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define GB_INLINE static
#define GB_LIKELY(x)   (x)
#define GB_UNLIKELY(x) (x)
#endif

/* Flags im F-Register */
#define FLAG_Z 0x80
#define FLAG_N 0x40
#define FLAG_H 0x20
#define FLAG_C 0x10

/* Interrupt-Bits */
#define INT_VBLANK 0x01
#define INT_STAT   0x02
#define INT_TIMER  0x04
#define INT_SERIAL 0x08
#define INT_JOYPAD 0x10

/* Joypad-Bits (gesetzt = gedrueckt) */
#define BTN_A      0x01
#define BTN_B      0x02
#define BTN_SELECT 0x04
#define BTN_START  0x08
#define BTN_RIGHT  0x10
#define BTN_LEFT   0x20
#define BTN_UP     0x40
#define BTN_DOWN   0x80

#define GB_W 160
#define GB_H 144
#define CYCLES_PER_FRAME 70224
#define APU_RATE 32768          /* Ausgabe-Samplerate (Hz) */
#define APU_BUF_SAMPLES 4096     /* Stereo-Frames im Ringpuffer */

/* Farbmodi */
#define COLOR_DMG 0   /* klassisch gruen/grau */
#define COLOR_GBC 1   /* Game-Boy-Color-Kompatibilitaetspalette */
#define COLOR_SGB 2   /* Super-Game-Boy-Paletten + Rahmen */

typedef struct {
    U8 a, f, b, c, d, e, h, l;
    U16 sp, pc;
    U8 ime;
    U8 ime_delay;   /* EI wirkt erst nach dem naechsten Befehl */
    U8 halted;
} GBCpu;

typedef struct {
    /* Pulse 1/2, Wave, Noise */
    U8  on[4];
    U8  dac[4];
    S32 length[4];
    U8  vol[4];
    U8  env_dir[4];
    U8  env_period[4];
    U8  env_timer[4];
    S32 freq_timer[4];
    U8  duty_pos[2];
    U8  wave_pos;
    U16 lfsr;
    /* Sweep */
    U8  sweep_on;
    U16 sweep_shadow;
    U8  sweep_timer;
    /* Frame-Sequencer */
    U32 fs_clock;
    U8  fs_step;
    /* Zeitbasis */
    U32 clock;         /* zuletzt synchronisierter Zyklus */
    U32 sample_acc;    /* Zyklen bis zum naechsten Sample (Festkomma) */
    S32 cap_l, cap_r;  /* Hochpass */
    /* Ausgabe-Ringpuffer (Stereo, interleaved) */
    short buf[APU_BUF_SAMPLES * 2];
    U32 wr, rd;
} GBApu;

typedef struct {
    /* Paketempfang ueber P1 */
    U8  receiving;
    U8  bitpos;
    U8  packet[16];
    U8  bytes[16 * 7];
    U8  npackets;
    U8  cur_packet;
    U8  last_p1;
    /* Multiplayer (MLT_REQ) - nur fuer die SGB-Erkennung */
    U8  mlt;
    U8  joy_id;
    /* Paletten: 4 Systempaletten fuer den Bildschirm, 512 aus PAL_TRN */
    U16 pal[4][4];
    U16 sys_pal[512][4];
    U8  attr[18][20];      /* Palette pro 8x8-Kachel */
    U8  attr_files[45][90];
    U8  mask;              /* MASK_EN */
    /* Rahmen (CHR_TRN/PCT_TRN) */
    U8  border_tiles[256 * 32];
    U16 border_map[32 * 32];
    U16 border_pal[64];
    U8  border_valid;
    U8  pending_trn;       /* 0=nichts, sonst Befehl der beim naechsten Frame VRAM liest */
    U8  pending_arg;
} GBSgb;

typedef struct {
    GBCpu cpu;

    /* Speicher */
    const U8 *rom;
    U32 rom_size;
    U16 rom_bank;
    U8  ram_bank;
    U8  ram_enable;
    U8  vram[0x2000];
    U8  wram[0x2000];
    U8  oam[0xA0];
    U8  hram[0x80];
    U8  io[0x80];
    U8  ie;
    U8  sram[0x8000];
    U8  sram_dirty;

    /* Zeit */
    U32 cycles;        /* T-Zyklen seit Frame-Basis */
    U32 deadline;      /* naechstes Ereignis: ab hier muss synchronisiert werden */

    /* PPU */
    U32 ppu_clock;
    U32 ppu_line_cycle;
    U8  ppu_mode;
    U8  window_line;
    U8  frame_done;
    U8  lcd_was_off;
    U8  stat_line;
    U16 frame[GB_W * GB_H];      /* fertiges Bild (RGB565) */
    U8  shade[GB_W * GB_H];      /* Rohwert 0-3 nach Palette */
    U8  layer[GB_W * GB_H];      /* 0=BG, 1=OBP0, 2=OBP1 */

    /* Timer */
    U32 timer_clock;
    U16 div_counter;

    /* Serial */
    U32 serial_end;
    U8  serial_active;

    /* Joypad */
    U8  buttons;

    /* Kampfmenue-Ausblendung (UI): ersetzt die 20x18 Hintergrundkacheln und
     * blendet Sprites/Fenster aus, solange bg_override_on gesetzt ist */
    U8  bg_override_on;
    U8  bg_override[20 * 18];

    /* Farbmodus */
    U8  color_mode;

    GBApu apu;
    GBSgb sgb;

    /* Statistik/Debug */
    U32 interp_steps;
    U32 dispatch_count;
} GB;

extern GB g_gb;

/* Rekompilierter Code: Tabelle PC -> Funktion. Eine Funktion fuehrt ab
 * dem Einstiegspunkt aus und kehrt zum Dispatcher zurueck, sobald der
 * Kontrollfluss die Region verlaesst; g_gb.cpu.pc zeigt dann auf den
 * naechsten Befehl. */
typedef void (*GBBlockFn)(void);

/* ---- API ---- */
void gb_init(const U8 *rom, U32 rom_size);
void gb_reset(void);
void gb_run_frame(void);                 /* fuehrt genau einen Frame aus */
void gb_set_buttons(U8 buttons);
void gb_set_color_mode(U8 mode);
U32  gb_audio_read(short *out, U32 max_frames);
void gb_set_recompiled(int enabled);     /* 0 = nur Interpreter */

/* Speicher */
U8   gb_read(U16 addr);
void gb_write(U16 addr, U8 val);
void gb_sync(void);
U8   gb_rom_read_banked(U8 bank, U16 addr);   /* fuer Frontend/Bottom-Screen */

/* Interne Schnittstellen */
void gb_ppu_sync(void);
void gb_ppu_write(U16 addr, U8 val);
void gb_ppu_reset(void);
void gb_ppu_compose_frame(void);
void gb_ppu_redraw(void);
void gb_apu_reset(void);
void gb_apu_sync(void);
void gb_apu_write(U16 addr, U8 val);
U8   gb_apu_read(U16 addr);
void gb_apu_rebase(U32 delta);
void gb_sgb_reset(void);
void gb_sgb_p1_write(U8 val);
void gb_sgb_frame(void);
U8   gb_sgb_p1_read(U8 val);
void gb_timer_sync(void);
void gb_update_deadline(void);
int  gb_interp_step(void);              /* ein Befehl, gibt Zyklen zurueck */
void gb_service_interrupts(void);

/* Rekompilierte Tabelle (vom Generator erzeugt) */
void       recomp_register_all(void);
void       recomp_register(U8 bank, U16 addr, GBBlockFn fn);
GBBlockFn  recomp_lookup(U16 pc);
extern int recomp_available;

/* Wird aufgerufen, wenn ROM-Code mangels Rekompilat interpretiert wird */
extern void (*gb_miss_hook)(U16 bank, U16 pc);

/* Fehlerbehandlung/Logging (Frontend implementiert) */
void gb_log(const char *fmt, ...);

#endif
