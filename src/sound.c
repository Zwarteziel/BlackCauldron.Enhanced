/* Black Cauldron Enhanced - PCjr / Tandy 1000 (SN76496) sound.
 * GPL-3.0-or-later. */
#include "sound.h"
#include <string.h>
#include <math.h>

#define CLOCK 3579545.0

static float att_vol[16];

void synth_init(Synth *s, int rate) {
    memset(s, 0, sizeof *s);
    s->rate = rate;
    s->lfsr = 0x4000;
    for (int i = 0; i < 15; i++) att_vol[i] = (float)pow(10.0, -0.1 * i);   /* 2 dB per step */
    att_vol[15] = 0;
}

static void next_note(Synth *s, Voice *v, int idx) {
    if (!v->notes || v->notes + 5 > v->end) { v->active = 0; return; }
    const uint8_t *p = v->notes;
    int dur = p[0] | (p[1] << 8);
    if (dur == 0xFFFF) { v->active = 0; return; }
    if (idx == 3) {
        v->noise_ctrl = p[3] & 7;
        s->lfsr = 0x4000;
    } else {
        v->divider = ((p[2] & 0x3F) << 4) | (p[3] & 0x0F);
    }
    v->atten = p[4] & 0x0F;
    v->left += dur * (double)s->rate / 60.0;
    v->notes += 5;
}

void synth_start(Synth *s, const uint8_t *d, int len) {
    if (len > (int)sizeof s->data) len = sizeof s->data;
    memcpy(s->data, d, len);
    s->len = len;
    for (int i = 0; i < 4; i++) {
        Voice *v = &s->v[i];
        memset(v, 0, sizeof *v);
        if (2 * i + 1 >= len) continue;
        int off = s->data[2 * i] | (s->data[2 * i + 1] << 8);
        if (off >= len) continue;
        v->notes = s->data + off;
        v->end = s->data + len;
        v->active = 1;
        v->atten = 15;
        next_note(s, v, i);
    }
    s->playing = 1;
}

void synth_stop(Synth *s) {
    s->playing = 0;
    for (int i = 0; i < 4; i++) s->v[i].active = 0;
}

void synth_render(Synth *s, int16_t *out, int n) {
    for (int i = 0; i < n; i++) {
        float mix = 0;
        if (s->playing) {
            int any = 0;
            for (int c = 0; c < 4; c++) {
                Voice *v = &s->v[c];
                if (!v->active) continue;
                any = 1;
                while (v->active && v->left <= 0) next_note(s, v, c);
                if (!v->active) continue;
                v->left -= 1;
                float amp = att_vol[v->atten];
                if (s->pcspeaker) {
                    /* PC speaker: the first voice only, on or off */
                    if (c != 0) continue;
                    amp = v->atten < 15 ? 0.7f : 0.0f;
                }
                if (c < 3) {
                    if (v->divider > 1 && amp > 0) {
                        double f = CLOCK / (32.0 * v->divider);
                        if (f < s->rate / 2.0) {
                            v->phase += f / s->rate;
                            if (v->phase >= 1) v->phase -= floor(v->phase);
                            mix += (v->phase < 0.5 ? 1.0f : -1.0f) * amp;
                        }
                    }
                } else if (amp > 0) {
                    int r = v->noise_ctrl & 3;
                    double f = r == 0 ? CLOCK / 512 : r == 1 ? CLOCK / 1024 : r == 2 ? CLOCK / 2048
                             : (s->v[2].divider > 0 ? CLOCK / (16.0 * s->v[2].divider) : CLOCK / 2048);
                    s->noise_phase += f / s->rate;
                    while (s->noise_phase >= 1) {
                        s->noise_phase -= 1;
                        int fb = (v->noise_ctrl & 4) ? ((s->lfsr ^ (s->lfsr >> 1)) & 1) : (s->lfsr & 1);
                        s->lfsr = (uint16_t)((s->lfsr >> 1) | (fb << 14));
                    }
                    mix += ((s->lfsr & 1) ? 1.0f : -1.0f) * amp;
                }
            }
            if (!any) s->playing = 0;
        }
        /* low-pass (~5 kHz), like the small speaker of a PC or monitor:
         * takes the sharpest edge off the high tunes */
        s->lp += (mix - s->lp) * 0.50f;
        float o = s->lp * 0.14f * 32767.0f;
        if (o > 32767) o = 32767;
        if (o < -32768) o = -32768;
        out[i] = (int16_t)o;
    }
}
