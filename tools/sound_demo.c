/* Renders an AGI sound resource to a WAV file, as PCjr/Tandy or AdLib.
 * Usage: sound_demo <gamedir> <sound nr> <tandy|flute|organ|harp|flute-harp> <out.wav>
 * GPL-3.0-or-later. */
#include "../src/agi.h"
#include "../src/sound.h"
#include "../src/adlib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(int argc, char **argv) {
    if (argc < 5) { fprintf(stderr, "usage: sound_demo gamedir nr style out.wav\n"); return 1; }
    static Agi e;
    if (agi_load(&e, argv[1]) < 0) { fprintf(stderr, "%s\n", e.err); return 1; }
    int len;
    const uint8_t *d = agi_res(&e, e.snddir[atoi(argv[2])], &len);
    if (!d) { fprintf(stderr, "no such sound\n"); return 1; }
    int ms = sound_duration_ms(d, len);
    if (ms > 30000) ms = 30000;
    int n = 44100 * (ms + 600) / 1000;
    int16_t *buf = malloc((size_t)n * 2);
    if (!strcmp(argv[3], "tandy")) {
        static Synth s; synth_init(&s, 44100); synth_start(&s, d, len); synth_render(&s, buf, n);
    } else {
        int style = !strcmp(argv[3], "organ") ? ADLIB_ORGAN : !strcmp(argv[3], "harp") ? ADLIB_HARP :
                    !strcmp(argv[3], "flute-harp") ? ADLIB_FLUTE_HARP : ADLIB_FLUTE;
        static AdlibSynth a; adlib_init(&a, 44100, style); adlib_start(&a, d, len); adlib_render(&a, buf, n);
    }
    FILE *f = fopen(argv[4], "wb");
    int32_t v; int16_t h;
    fwrite("RIFF", 1, 4, f); v = 36 + n * 2; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    v = 16; fwrite(&v, 4, 1, f); h = 1; fwrite(&h, 2, 1, f); fwrite(&h, 2, 1, f);
    v = 44100; fwrite(&v, 4, 1, f); v = 88200; fwrite(&v, 4, 1, f); h = 2; fwrite(&h, 2, 1, f); h = 16; fwrite(&h, 2, 1, f);
    fwrite("data", 1, 4, f); v = n * 2; fwrite(&v, 4, 1, f); fwrite(buf, 2, (size_t)n, f); fclose(f);
    double rms = 0; int peak = 0;
    for (int i = 0; i < n; i++) { rms += (double)buf[i] * buf[i]; if (abs(buf[i]) > peak) peak = abs(buf[i]); }
    printf("sound %s %s: %.1f s, rms %.0f, peak %d\n", argv[2], argv[3], n / 44100.0, sqrt(rms / n), peak);
    return 0;
}
