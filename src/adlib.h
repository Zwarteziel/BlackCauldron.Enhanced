/* Black Cauldron Enhanced - AdLib (OPL2) arrangement of AGI sound resources.
 * The three tone voices become FM channels with a chosen instrument, the
 * noise voice an FM noise sound. Uses Nuked-OPL3 (LGPL-2.1-or-later).
 * GPL-3.0-or-later. */
#ifndef BCE_ADLIB_H
#define BCE_ADLIB_H
#include <stdint.h>
#include "../third_party/nuked-opl3/opl3.h"

typedef struct {
    uint8_t mod_char, mod_tl, mod_ad, mod_sr, mod_ws;
    uint8_t car_char, car_tl, car_ad, car_sr, car_ws;
    uint8_t fbcnt;
} OplPatch;

typedef struct {
    const uint8_t *notes, *end;
    double left;
    int divider, atten, noise_ctrl;
    int on, pending_on, active;
    int cur_fnum, cur_block;
} AdlVoice;

typedef struct {
    opl3_chip chip;
    int rate;
    int style;
    uint8_t data[65536];
    int len;
    AdlVoice v[4];
    int playing;
} AdlibSynth;

enum { ADLIB_FLUTE, ADLIB_ORGAN, ADLIB_HARP, ADLIB_FLUTE_HARP, ADLIB_STYLES };

void adlib_init(AdlibSynth *s, int rate, int style);
void adlib_start(AdlibSynth *s, const uint8_t *d, int len);
void adlib_stop(AdlibSynth *s);
void adlib_render(AdlibSynth *s, int16_t *out, int n);
#endif
