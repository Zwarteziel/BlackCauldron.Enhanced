/* Black Cauldron Enhanced - AdLib (OPL2) arrangement of AGI sound resources.
 * GPL-3.0-or-later. */
#include "adlib.h"
#include <string.h>
#include <math.h>

#define CLOCK 3579545.0
/* operator register offsets of OPL2 channels 0..8 (modulator; carrier +3) */
static const uint8_t OP_OFF[9] = {0x00, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x10, 0x11, 0x12};

/* Instrument sets: tone voices 0-2 and the noise voice. */
static const OplPatch PATCH_TONE[3] = {
    /* flute: soft, nearly sine, a little breath and vibrato */
    {0x61, 0x2A, 0x71, 0x06, 0x00,  0x61, 0x00, 0x72, 0x07, 0x00,  0x06},
    /* organ: two sine operators added (fundamental and octave) */
    {0x21, 0x12, 0xF0, 0x05, 0x00,  0x22, 0x08, 0xF0, 0x05, 0x00,  0x01},
    /* harp: plucked, decaying FM tone */
    {0x01, 0x1A, 0xF3, 0x55, 0x00,  0x01, 0x00, 0xF2, 0x74, 0x00,  0x0C},
};
/* bass voice (the third tone voice): rounder, a bit louder */
static const OplPatch PATCH_BASS[3] = {
    {0x21, 0x2C, 0x81, 0x06, 0x00,  0x21, 0x00, 0x82, 0x07, 0x00,  0x04},
    {0x21, 0x10, 0xF0, 0x05, 0x00,  0x21, 0x06, 0xF0, 0x05, 0x00,  0x01},
    {0x01, 0x1E, 0xF3, 0x45, 0x00,  0x01, 0x00, 0xF2, 0x65, 0x00,  0x0A},
};
/* noise: modulator with full feedback at a high multiple gives noise */
static const OplPatch PATCH_NOISE =
    {0x2F, 0x00, 0xF0, 0x05, 0x00,  0x2E, 0x04, 0xF0, 0x06, 0x00,  0x0E};

static void wr(AdlibSynth *s, int reg, int v) { OPL3_WriteReg(&s->chip, (uint16_t)reg, (uint8_t)v); }

static void set_patch(AdlibSynth *s, int ch, const OplPatch *p, int atten) {
    int m = OP_OFF[ch], c = m + 3;
    int add = (atten * 8 + 1) / 3;                /* 2 dB per AGI step, 0.75 dB per TL step */
    int ctl = p->car_tl + add; if (ctl > 63) ctl = 63;
    int mtl = p->mod_tl;
    if (p->fbcnt & 1) { mtl += add; if (mtl > 63) mtl = 63; }   /* additive: both operators sound */
    wr(s, 0x20 + m, p->mod_char); wr(s, 0x40 + m, mtl);  wr(s, 0x60 + m, p->mod_ad);
    wr(s, 0x80 + m, p->mod_sr);   wr(s, 0xE0 + m, p->mod_ws);
    wr(s, 0x20 + c, p->car_char); wr(s, 0x40 + c, ctl);  wr(s, 0x60 + c, p->car_ad);
    wr(s, 0x80 + c, p->car_sr);   wr(s, 0xE0 + c, p->car_ws);
    wr(s, 0xC0 + ch, p->fbcnt);
}

static const OplPatch *patch_for(AdlibSynth *s, int voice) {
    if (voice == 3) return &PATCH_NOISE;
    if (s->style == ADLIB_FLUTE_HARP) {
        /* melody (first voice) on the flute, the accompaniment on the harp */
        if (voice == 0) return &PATCH_TONE[ADLIB_FLUTE];
        if (voice == 1) return &PATCH_TONE[ADLIB_HARP];
        return &PATCH_BASS[ADLIB_HARP];
    }
    if (voice == 2) return &PATCH_BASS[s->style];
    return &PATCH_TONE[s->style];
}

static void freq_to_fnum(double f, int *fnum, int *block) {
    int b = 0;
    double fn = f * 1048576.0 / 49716.0;          /* 2^20 */
    while (fn >= 1024 && b < 7) { fn /= 2; b++; }
    if (fn > 1023) fn = 1023;
    *fnum = (int)(fn + 0.5);
    *block = b;
}

void adlib_init(AdlibSynth *s, int rate, int style) {
    memset(s, 0, sizeof *s);
    s->rate = rate;
    s->style = style < 0 || style >= ADLIB_STYLES ? 0 : style;
    OPL3_Reset(&s->chip, (uint32_t)rate);
    wr(s, 0x01, 0x20);                            /* allow waveform select */
}

static void key(AdlibSynth *s, int ch, AdlVoice *v, int on) {
    wr(s, 0xA0 + ch, v->cur_fnum & 0xFF);
    wr(s, 0xB0 + ch, (on ? 0x20 : 0) | (v->cur_block << 2) | ((v->cur_fnum >> 8) & 3));
    v->on = on;
}

static void next_note(AdlibSynth *s, AdlVoice *v, int ch) {
    if (!v->notes || v->notes + 5 > v->end) { v->active = 0; key(s, ch, v, 0); return; }
    const uint8_t *p = v->notes;
    int dur = p[0] | (p[1] << 8);
    if (dur == 0xFFFF) { v->active = 0; key(s, ch, v, 0); return; }
    int old_div = v->divider, old_noise = v->noise_ctrl, was_on = v->on;
    if (ch == 3) v->noise_ctrl = p[3] & 7;
    else v->divider = ((p[2] & 0x3F) << 4) | (p[3] & 0x0F);
    v->atten = p[4] & 0x0F;
    v->left += dur * (double)s->rate / 60.0;
    v->notes += 5;
    if (v->atten >= 15 || (ch < 3 && v->divider < 2)) { if (v->on) key(s, ch, v, 0); return; }
    double f;
    if (ch == 3) {
        int r = v->noise_ctrl & 3;
        f = r == 0 ? 1400 : r == 1 ? 900 : r == 2 ? 500 : 700;   /* brightness of the noise */
    } else f = CLOCK / (32.0 * v->divider);
    freq_to_fnum(f, &v->cur_fnum, &v->cur_block);
    set_patch(s, ch, patch_for(s, ch), v->atten);
    int new_note = !was_on || (ch < 3 ? old_div != v->divider : old_noise != v->noise_ctrl);
    if (new_note) {
        if (was_on) { key(s, ch, v, 0); v->pending_on = 1; }   /* retrigger on the next sample */
        else key(s, ch, v, 1);
    } else key(s, ch, v, 1);                       /* same note: only the volume changed */
}

void adlib_start(AdlibSynth *s, const uint8_t *d, int len) {
    adlib_stop(s);
    if (len > (int)sizeof s->data) len = sizeof s->data;
    memcpy(s->data, d, len);
    s->len = len;
    for (int i = 0; i < 4; i++) {
        AdlVoice *v = &s->v[i];
        memset(v, 0, sizeof *v);
        if (2 * i + 1 >= len) continue;
        int off = s->data[2 * i] | (s->data[2 * i + 1] << 8);
        if (off >= len) continue;
        v->notes = s->data + off;
        v->end = s->data + len;
        v->active = 1;
        next_note(s, v, i);
    }
    s->playing = 1;
}

void adlib_stop(AdlibSynth *s) {
    for (int i = 0; i < 4; i++) { if (s->v[i].on) key(s, i, &s->v[i], 0); s->v[i].active = 0; }
    s->playing = 0;
}

void adlib_render(AdlibSynth *s, int16_t *out, int n) {
    int16_t st[2];
    for (int i = 0; i < n; i++) {
        if (s->playing) {
            int any = 0;
            for (int c = 0; c < 4; c++) {
                AdlVoice *v = &s->v[c];
                if (v->pending_on) { key(s, c, v, 1); v->pending_on = 0; }
                if (!v->active) continue;
                any = 1;
                while (v->active && v->left <= 0) next_note(s, v, c);
                v->left -= 1;
            }
            if (!any) s->playing = 0;
        }
        OPL3_GenerateResampled(&s->chip, st);
        int m = (st[0] + st[1]) / 2;
        m = m * 3 / 2;
        if (m > 32767) m = 32767;
        if (m < -32768) m = -32768;
        out[i] = (int16_t)m;
    }
}
