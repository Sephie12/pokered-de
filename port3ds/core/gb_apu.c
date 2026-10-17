/*
 * gb_apu.c - DMG-Soundchip (2x Pulse, Wave, Noise), 32768 Hz Stereo.
 * Wird lazy synchronisiert: bei Registerzugriffen und am Frame-Ende.
 */
#include <string.h>
#include "gb.h"

#define A (g_gb.apu)
#define R(x) (g_gb.io[(x) - 0xFF00 + 0x00])
#define CYC_PER_SAMPLE 128

static const U8 k_duty[4] = { 0x01, 0x81, 0x87, 0x7E };
static const U8 k_noise_div[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

void gb_apu_reset(void)
{
    memset(&A, 0, sizeof(A));
    A.lfsr = 0x7FFF;
    g_gb.io[0x26] = 0xF1;
    g_gb.io[0x24] = 0x77;
    g_gb.io[0x25] = 0xF3;
}

void gb_apu_rebase(U32 delta)
{
    A.clock -= delta;
}

static U16 ch_freq(int ch)
{
    U8 lo, hi;
    if (ch == 0)      { lo = g_gb.io[0x13]; hi = g_gb.io[0x14]; }
    else if (ch == 1) { lo = g_gb.io[0x18]; hi = g_gb.io[0x19]; }
    else              { lo = g_gb.io[0x1D]; hi = g_gb.io[0x1E]; }
    return (U16)(lo | ((hi & 7) << 8));
}

static void set_freq(int ch, U16 f)
{
    if (ch == 0) {
        g_gb.io[0x13] = (U8)f;
        g_gb.io[0x14] = (U8)((g_gb.io[0x14] & 0xF8) | ((f >> 8) & 7));
    }
}

static U32 period(int ch)
{
    if (ch < 2) return (U32)(2048 - ch_freq(ch)) * 4;
    if (ch == 2) return (U32)(2048 - ch_freq(2)) * 2;
    {
        U8 nr43;
        nr43 = g_gb.io[0x22];
        return (U32)k_noise_div[nr43 & 7] << (nr43 >> 4);
    }
}

static U16 sweep_calc(void)
{
    U8 nr10;
    U16 delta, nf;
    nr10 = g_gb.io[0x10];
    delta = (U16)(A.sweep_shadow >> (nr10 & 7));
    if (nr10 & 0x08) nf = (U16)(A.sweep_shadow - delta);
    else nf = (U16)(A.sweep_shadow + delta);
    if (nf > 2047) A.on[0] = 0;
    return nf;
}

static void clock_length(void)
{
    static const U8 nrx4[4] = { 0x14, 0x19, 0x1E, 0x23 };
    int ch;
    for (ch = 0; ch < 4; ch++) {
        if ((g_gb.io[nrx4[ch]] & 0x40) && A.length[ch] > 0) {
            A.length[ch]--;
            if (A.length[ch] == 0) A.on[ch] = 0;
        }
    }
}

static void clock_env(void)
{
    static const U8 nrx2[3] = { 0x12, 0x17, 0x21 };
    static const int chs[3] = { 0, 1, 3 };
    int i;
    for (i = 0; i < 3; i++) {
        int ch;
        ch = chs[i];
        if (A.env_period[ch] == 0) continue;
        if (A.env_timer[ch] > 0) A.env_timer[ch]--;
        if (A.env_timer[ch] == 0) {
            A.env_timer[ch] = A.env_period[ch];
            if (A.env_dir[ch] && A.vol[ch] < 15) A.vol[ch]++;
            else if (!A.env_dir[ch] && A.vol[ch] > 0) A.vol[ch]--;
        }
    }
    (void)nrx2;
}

static void clock_sweep(void)
{
    U8 nr10, per;
    nr10 = g_gb.io[0x10];
    per = (U8)((nr10 >> 4) & 7);
    if (A.sweep_timer > 0) A.sweep_timer--;
    if (A.sweep_timer == 0) {
        A.sweep_timer = per ? per : 8;
        if (A.sweep_on && per) {
            U16 nf;
            nf = sweep_calc();
            if (nf <= 2047 && (nr10 & 7)) {
                A.sweep_shadow = nf;
                set_freq(0, nf);
                (void)sweep_calc();
            }
        }
    }
}

static void frame_seq(void)
{
    switch (A.fs_step) {
    case 0: case 4: clock_length(); break;
    case 2: case 6: clock_length(); clock_sweep(); break;
    case 7: clock_env(); break;
    default: break;
    }
    A.fs_step = (U8)((A.fs_step + 1) & 7);
}

static int ch_output(int ch)
{
    if (!A.on[ch] || !A.dac[ch]) return 0;
    if (ch < 2) {
        U8 duty;
        duty = (U8)(g_gb.io[ch == 0 ? 0x11 : 0x16] >> 6);
        return ((k_duty[duty] >> A.duty_pos[ch]) & 1) ? A.vol[ch] : 0;
    }
    if (ch == 2) {
        U8 s, code;
        s = g_gb.io[0x30 + (A.wave_pos >> 1)];
        s = (U8)((A.wave_pos & 1) ? (s & 0xF) : (s >> 4));
        code = (U8)((g_gb.io[0x1C] >> 5) & 3);
        if (code == 0) return 0;
        return s >> (code - 1);
    }
    return (A.lfsr & 1) ? 0 : A.vol[3];
}

static void advance_channels(U32 cyc)
{
    int ch;
    for (ch = 0; ch < 4; ch++) {
        U32 p;
        if (!A.on[ch]) continue;
        p = period(ch);
        if (p == 0) p = 1;
        A.freq_timer[ch] -= (S32)cyc;
        while (A.freq_timer[ch] <= 0) {
            A.freq_timer[ch] += (S32)p;
            if (ch < 2) {
                A.duty_pos[ch] = (U8)((A.duty_pos[ch] + 1) & 7);
            } else if (ch == 2) {
                A.wave_pos = (U8)((A.wave_pos + 1) & 31);
            } else {
                U16 x;
                x = (U16)((A.lfsr ^ (A.lfsr >> 1)) & 1);
                A.lfsr = (U16)((A.lfsr >> 1) | (x << 14));
                if (g_gb.io[0x22] & 0x08) {
                    A.lfsr = (U16)((A.lfsr & ~0x40) | (x << 6));
                }
            }
        }
    }
}

static void emit_sample(void)
{
    S32 l, r, v;
    int ch;
    U8 nr51, nr50;
    U32 next;

    l = 0;
    r = 0;
    nr51 = g_gb.io[0x25];
    nr50 = g_gb.io[0x24];
    if (g_gb.io[0x26] & 0x80) {
        for (ch = 0; ch < 4; ch++) {
            if (!A.dac[ch]) continue;
            v = ch_output(ch) * 2 - 15;
            if (!A.on[ch]) v = -15 + 15;
            if (nr51 & (0x10 << ch)) l += v;
            if (nr51 & (0x01 << ch)) r += v;
        }
    }
    l = l * (((nr50 >> 4) & 7) + 1) * 64;
    r = r * ((nr50 & 7) + 1) * 64;
    /* Hochpass gegen DC-Offset */
    A.cap_l += ((l << 8) - A.cap_l) >> 9;
    A.cap_r += ((r << 8) - A.cap_r) >> 9;
    l -= A.cap_l >> 8;
    r -= A.cap_r >> 8;
    if (l > 32767) l = 32767;
    if (l < -32768) l = -32768;
    if (r > 32767) r = 32767;
    if (r < -32768) r = -32768;
    next = (A.wr + 1) % APU_BUF_SAMPLES;
    if (next == A.rd) {
        return;   /* Puffer voll: Sample verwerfen */
    }
    A.buf[A.wr * 2] = (short)l;
    A.buf[A.wr * 2 + 1] = (short)r;
    A.wr = next;
}

void gb_apu_sync(void)
{
    U32 elapsed;
    elapsed = g_gb.cycles - A.clock;
    if ((S32)elapsed <= 0) return;
    A.clock = g_gb.cycles;
    A.sample_acc += elapsed;
    while (A.sample_acc >= CYC_PER_SAMPLE) {
        A.sample_acc -= CYC_PER_SAMPLE;
        advance_channels(CYC_PER_SAMPLE);
        A.fs_clock += CYC_PER_SAMPLE;
        if (A.fs_clock >= 8192) {
            A.fs_clock -= 8192;
            frame_seq();
        }
        emit_sample();
    }
}

static void trigger(int ch)
{
    static const U8 nrx2[4] = { 0x12, 0x17, 0x1C, 0x21 };
    U8 env;
    A.on[ch] = A.dac[ch];
    if (A.length[ch] == 0) A.length[ch] = (ch == 2) ? 256 : 64;
    A.freq_timer[ch] = (S32)period(ch);
    if (ch == 2) {
        A.wave_pos = 0;
        return;
    }
    env = g_gb.io[nrx2[ch]];
    A.vol[ch] = (U8)(env >> 4);
    A.env_dir[ch] = (U8)((env >> 3) & 1);
    A.env_period[ch] = (U8)(env & 7);
    A.env_timer[ch] = A.env_period[ch];
    if (ch == 3) A.lfsr = 0x7FFF;
    if (ch == 0) {
        U8 nr10, per;
        nr10 = g_gb.io[0x10];
        per = (U8)((nr10 >> 4) & 7);
        A.sweep_shadow = ch_freq(0);
        A.sweep_timer = per ? per : 8;
        A.sweep_on = (U8)(per || (nr10 & 7));
        if (nr10 & 7) (void)sweep_calc();
    }
}

void gb_apu_write(U16 addr, U8 val)
{
    U8 reg;
    reg = (U8)(addr & 0x7F);
    gb_apu_sync();
    if (!(g_gb.io[0x26] & 0x80) && reg != 0x26 && reg < 0x30) {
        return;   /* APU aus: Register gesperrt */
    }
    switch (reg) {
    case 0x11: A.length[0] = 64 - (val & 63); break;
    case 0x16: A.length[1] = 64 - (val & 63); break;
    case 0x1B: A.length[2] = 256 - val; break;
    case 0x20: A.length[3] = 64 - (val & 63); break;
    case 0x12: A.dac[0] = (U8)((val & 0xF8) != 0); if (!A.dac[0]) A.on[0] = 0; break;
    case 0x17: A.dac[1] = (U8)((val & 0xF8) != 0); if (!A.dac[1]) A.on[1] = 0; break;
    case 0x1A: A.dac[2] = (U8)((val & 0x80) != 0); if (!A.dac[2]) A.on[2] = 0; break;
    case 0x21: A.dac[3] = (U8)((val & 0xF8) != 0); if (!A.dac[3]) A.on[3] = 0; break;
    case 0x26:
        if (!(val & 0x80)) {
            int i;
            for (i = 0x10; i < 0x26; i++) g_gb.io[i] = 0;
            memset(A.on, 0, sizeof(A.on));
            memset(A.dac, 0, sizeof(A.dac));
        }
        g_gb.io[0x26] = (U8)((g_gb.io[0x26] & 0x7F) | (val & 0x80));
        return;
    default: break;
    }
    g_gb.io[reg] = val;
    if (val & 0x80) {
        if (reg == 0x14) trigger(0);
        else if (reg == 0x19) trigger(1);
        else if (reg == 0x1E) trigger(2);
        else if (reg == 0x23) trigger(3);
    }
}

U8 gb_apu_read(U16 addr)
{
    static const U8 mask[0x17] = {
        0x80, 0x3F, 0x00, 0xFF, 0xBF,
        0xFF, 0x3F, 0x00, 0xFF, 0xBF,
        0x7F, 0xFF, 0x9F, 0xFF, 0xBF,
        0xFF, 0xFF, 0x00, 0x00, 0xBF,
        0x00, 0x00, 0x70
    };
    U8 reg;
    reg = (U8)(addr & 0x7F);
    gb_apu_sync();
    if (reg == 0x26) {
        return (U8)((g_gb.io[0x26] & 0x80) | 0x70
            | (A.on[0] ? 1 : 0) | (A.on[1] ? 2 : 0) | (A.on[2] ? 4 : 0) | (A.on[3] ? 8 : 0));
    }
    if (reg >= 0x10 && reg < 0x27) {
        return (U8)(g_gb.io[reg] | mask[reg - 0x10]);
    }
    return g_gb.io[reg];
}

U32 gb_audio_read(short *out, U32 max_frames)
{
    U32 n;
    n = 0;
    while (n < max_frames && A.rd != A.wr) {
        out[n * 2] = A.buf[A.rd * 2];
        out[n * 2 + 1] = A.buf[A.rd * 2 + 1];
        A.rd = (A.rd + 1) % APU_BUF_SAMPLES;
        n++;
    }
    return n;
}
