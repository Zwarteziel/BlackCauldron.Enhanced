/* Black Cauldron Enhanced - PCjr / Tandy 1000 (SN76496) sound for AGI
 * sound resources: three square-wave voices and a noise voice.
 * GPL-3.0-or-later. */
#ifndef BCE_SOUND_H
#define BCE_SOUND_H
#include <stdint.h>

typedef struct {
    const uint8_t *notes;    /* next note (5 bytes each) */
    const uint8_t *end;
    double left;             /* samples left of the current note */
    int divider;             /* 10-bit frequency divider */
    int atten;               /* 0 loud .. 15 off */
    int noise_ctrl;          /* noise voice: bit 2 white, bits 0-1 rate */
    double phase;
    int out;
    int active;
} Voice;

typedef struct {
    int rate;
    uint8_t data[65536];
    int len;
    Voice v[4];
    uint16_t lfsr;
    double noise_phase;
    float lp;                /* gentle low-pass state */
    int playing;
    int pcspeaker;           /* 1 = only the first voice, as the PC speaker */
} Synth;

void synth_init(Synth *s, int rate);
void synth_start(Synth *s, const uint8_t *d, int len);
void synth_stop(Synth *s);
void synth_render(Synth *s, int16_t *out, int n);
#endif
