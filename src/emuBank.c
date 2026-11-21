/*
 * SynthLib - common library for synthesizer editor applications.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// Notes: Docs/code-notes/emuBank.c.md - "// notes §k" refers there.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "emuBank.h"

#define MAX_SAMPLES         (1000)
#define EBL_HEADER_BASE     (2)        // notes §3: the sample header's offsets count from here in the E5S1 chunk
#define EBL_START_L         (0x48)     // then start R, end L, end R, loop start L and R, loop end L and R
#define EBL_RATE            (0x68)
#define EBL_OPTIONS         (0x6e)
#define EBL_OPTION_LOOP     (0x0001)
#define ZHDR_SAMPLE         (4)
#define ZHDR_ROOT           (10)
#define ETW_LOW             (4)
#define ETW_HIGH            (7)
#define PHDR_NAME           (12)       // UTF-16, from the Phdr tag
#define E5EV_STAGES_AT      (10)       // notes §5: then six stages of a time and a level, big-endian floats
#define E5EV_STAGE_BYTES    (9)

struct tEmuBank {
    char         path[2048];
    uint8_t *    data;
    uint32_t     size;
    uint32_t     presetCount;
    uint32_t     presetBody[EMU_BANK_MAX_PRESETS];
    uint32_t     presetLength[EMU_BANK_MAX_PRESETS];
    char         presetName[EMU_BANK_MAX_PRESETS][EMU_BANK_PRESET_NAME_SIZE];
    tEmuSample * pool[MAX_SAMPLES + 1];   // sample number -> its data, NULL until read (or unreadable)
    bool         poolTried[MAX_SAMPLES + 1];
    bool         e4b;                     // notes §8: an E-mu EOS bank, samples inside it
    uint32_t     sampleBody[MAX_SAMPLES + 1];
    uint32_t     sampleLength[MAX_SAMPLES + 1];
};

static uint32_t read_u32_be(const uint8_t * p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static float read_f32_be(const uint8_t * p) {
    uint32_t u = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    float    f;

    memcpy(&f, &u, sizeof(f));
    return f;
}

static uint32_t read_u32_le(const uint8_t * p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint8_t * read_whole_file(const char * path, uint32_t * size) {
    FILE *    f = fopen(path, "rb");
    long      n;
    uint8_t * d = NULL;

    if (f == NULL) {
        return NULL;
    }

    if ((fseek(f, 0, SEEK_END) == 0) && ((n = ftell(f)) > 0) && (n < 0x7FFFFFFF) && (fseek(f, 0, SEEK_SET) == 0)) {
        d     = malloc((size_t)n);

        if ((d != NULL) && (fread(d, 1, (size_t)n, f) != (size_t)n)) {
            free(d);
            d = NULL;
        }
        *size = (uint32_t)n;
    }
    fclose(f);
    return d;
}

// The first chunk with this tag between `from` and `to`, at the top level; its body and length
static bool iff_find(const uint8_t * d, uint32_t from, uint32_t to, const char * tag, uint32_t * body, uint32_t * length) {
    for (uint32_t p = from; (p + 8u) <= to;) {
        uint32_t n = read_u32_be(d + p + 4);

        if (n > (to - p - 8u)) {
            return false;
        }

        if (memcmp(d + p, tag, 4) == 0) {
            *body   = p + 8u;
            *length = n;
            return true;
        }
        p += 8u + n + (n & 1u);
    }

    return false;
}

static void sample_free(tEmuSample * s) {
    if (s != NULL) {
        free(s->data[0]);

        if (s->data[1] != s->data[0]) {
            free(s->data[1]);
        }
        free(s);
    }
}

// notes §3: one .ebl - 16-bit little-endian frames, a stereo file's left channel whole and then its right
static tEmuSample * ebl_load(const char * path, uint32_t number) {
    uint32_t        size   = 0;
    uint8_t *       d      = read_whole_file(path, &size);
    uint32_t        body   = 0;
    uint32_t        length = 0;

    if (d == NULL) {
        return NULL;
    }

    if (  (size < 12) || (memcmp(d, "FORM", 4) != 0) || !iff_find(d, 12, size, "E5S1", &body, &length)
       || (length < (EBL_HEADER_BASE + EBL_OPTIONS + 2u))) {
        free(d);
        return NULL;
    }
    const uint8_t * h      = d + body + EBL_HEADER_BASE;
    uint32_t        limit  = length - EBL_HEADER_BASE;
    uint32_t        f[8];

    for (uint32_t i = 0; i < 8u; i++) {
        f[i] = read_u32_le(h + EBL_START_L + (4u * i));
    }

    uint32_t        startL = f[0], startR = f[1], endL = f[2], endR = f[3];
    uint32_t        rate   = read_u32_le(h + EBL_RATE);
    uint32_t        frames = ((endL >= startL) && (endL < limit)) ? (((endL - startL) / 2u) + 1u) : 0u;
    bool            stereo = (frames > 0u) && (startR > endL) && (endR < limit) && ((endR - startR) == (endL - startL));
    tEmuSample *    s      = calloc(1, sizeof(tEmuSample));

    if ((s == NULL) || (frames == 0u) || (rate < 1000u) || (rate > 192000u)) {
        free(s);
        free(d);
        return NULL;
    }
    s->channels  = stereo ? 2u : 1u;
    s->data[0]   = malloc(sizeof(float) * frames);
    s->data[1]   = stereo ? malloc(sizeof(float) * frames) : s->data[0];

    if ((s->data[0] == NULL) || (s->data[1] == NULL)) {
        sample_free(s);
        free(d);
        return NULL;
    }

    for (uint32_t c = 0; c < s->channels; c++) {
        const uint8_t * q = h + ((c == 0u) ? startL : startR);

        for (uint32_t i = 0; i < frames; i++) {
            s->data[c][i] = (float)(int16_t)((uint16_t)q[2u * i] | ((uint16_t)q[(2u * i) + 1u] << 8)) / 32768.0f;
        }
    }

    // notes §3: the stored loop end is the loop's last frame
    uint32_t loopS = (f[4] >= startL) ? ((f[4] - startL) / 2u) : 0u;
    uint32_t loopE = (f[6] >= startL) ? (((f[6] - startL) / 2u) + 1u) : 0u;

    s->frames    = frames;
    s->rate      = (double)rate;
    s->number    = number;
    s->loopEnd   = (loopE <= frames) ? loopE : frames;
    s->loopStart = loopS;
    s->looped    = ((((uint32_t)h[EBL_OPTIONS] | ((uint32_t)h[EBL_OPTIONS + 1] << 8)) & EBL_OPTION_LOOP) != 0u)
                   && ((loopS + 1u) < s->loopEnd);
    free(d);
    return s;
}

// notes §1: <bank folder>/SamplePool/<bank name>SL<nnn>.ebl
static bool sample_path(const char * bank, uint32_t number, char * out, size_t size) {
    const char * slash = strrchr(bank, '/');
    const char * back  = strrchr(bank, '\\');
    const char * leaf  = ((back != NULL) && ((slash == NULL) || (back > slash))) ? (back + 1) : ((slash != NULL) ? (slash + 1) : bank);
    const char * dot   = strrchr(leaf, '.');
    int          dir   = (int)(leaf - bank);
    int          stem  = (dot != NULL) ? (int)(dot - leaf) : (int)strlen(leaf);

    return snprintf(out, size, "%.*sSamplePool/%.*sSL%03u.ebl", dir, bank, stem, leaf, number) < (int)size;
}

typedef struct {
    tEmuBank *   bank;
    tEmuPreset * preset;
    int32_t      voiceKey[2];
    int32_t      voiceVel[2];
    uint32_t     voice;
    uint32_t     windows;       // ETW chunks seen since the voice or zone began
    uint32_t     envelopes;     // E5Ev chunks seen in the voice
    tEmuEnvelope amp;           // notes §5: the voice's first

    bool         inZones;
    bool         started;       // a voice seen yet
    int32_t      zone;          // the zone the windows after a Zhdr narrow, or -1
} tWalk;

static const tEmuSample * pool_sample(tEmuBank * bank, uint32_t number) {
    if (!bank->poolTried[number]) {
        char file[2200];

        bank->poolTried[number] = true;

        if (sample_path(bank->path, number, file, sizeof(file))) {
            bank->pool[number] = ebl_load(file, number);
        }
    }
    return bank->pool[number];
}

static void walk_zone(tWalk * w, const uint8_t * z, uint32_t length) {
    tEmuPreset *       preset = w->preset;
    uint32_t           number = (length > ZHDR_ROOT) ? (((uint32_t)z[ZHDR_SAMPLE] << 8) | z[ZHDR_SAMPLE + 1]) : 0u;
    const tEmuSample * sample = ((number > 0u) && (number <= MAX_SAMPLES)) ? pool_sample(w->bank, number) : NULL;

    w->zone        = -1;

    if ((sample == NULL) || (preset->zoneCount >= EMU_BANK_MAX_ZONES)) {
        return;
    }
    tEmuZone *         zone   = &preset->zone[preset->zoneCount++];

    zone->sample   = sample;
    zone->rootNote = z[ZHDR_ROOT];
    zone->keyLow   = w->voiceKey[0];
    zone->keyHigh  = w->voiceKey[1];
    zone->velLow   = w->voiceVel[0];
    zone->velHigh  = w->voiceVel[1];
    zone->voice    = w->voice;
    zone->amp      = w->amp;
    w->zone        = (int32_t)(preset->zoneCount - 1u);
    w->windows     = 0;
}

// notes §2: a voice's first two windows are its keys and velocities; a zone's narrow them
static void walk(tWalk * w, const uint8_t * d, uint32_t from, uint32_t to) {
    for (uint32_t p = from; (p + 8u) <= to;) {
        uint32_t        n    = read_u32_be(d + p + 4);
        const uint8_t * body = d + p + 8;

        if (n > (to - p - 8u)) {
            return;
        }

        if ((memcmp(d + p, "LIST", 4) == 0) && (n >= 4u)) {
            bool zones = (memcmp(body, "E5ZL", 4) == 0);

            w->inZones = w->inZones || zones;
            walk(w, d, p + 12u, p + 8u + n);
            w->inZones = w->inZones && !zones;
        } else if (memcmp(d + p, "E5V1", 4) == 0) {
            w->voice      += w->started ? 1u : 0u;
            w->started     = true;
            w->voiceKey[0] = 0;
            w->voiceKey[1] = 127;
            w->voiceVel[0] = 0;
            w->voiceVel[1] = 127;
            w->windows     = 0;
            w->zone        = -1;
            w->envelopes   = 0;
            memset(&w->amp, 0, sizeof(w->amp));
            walk(w, d, p + 8u, p + 8u + n);
        } else if ((memcmp(d + p, "E5Ev", 4) == 0) && (n >= (E5EV_STAGES_AT + (EMU_ENV_STAGES * E5EV_STAGE_BYTES)))) {
            if (w->envelopes++ == 0u) {   // notes §5: the amplifier's comes first
                w->amp.present = true;

                for (uint32_t s = 0; s < EMU_ENV_STAGES; s++) {
                    w->amp.time[s]  = read_f32_be(body + E5EV_STAGES_AT + (s * E5EV_STAGE_BYTES));
                    w->amp.level[s] = read_f32_be(body + E5EV_STAGES_AT + (s * E5EV_STAGE_BYTES) + 4u);
                }
            }
        } else if (memcmp(d + p, "Zhdr", 4) == 0) {
            walk_zone(w, body, n);
        } else if ((memcmp(d + p, "ETW ", 4) == 0) && (n > ETW_HIGH)) {
            int32_t lo = body[ETW_LOW];
            int32_t hi = body[ETW_HIGH];

            if (!w->inZones && (w->windows < 2u)) {
                int32_t * range = (w->windows == 0u) ? w->voiceKey : w->voiceVel;

                range[0] = lo;
                range[1] = hi;
            } else if (w->inZones && (w->zone >= 0) && (w->windows < 2u)) {
                tEmuZone * zone = &w->preset->zone[w->zone];
                int32_t *  low  = (w->windows == 0u) ? &zone->keyLow : &zone->velLow;
                int32_t *  high = (w->windows == 0u) ? &zone->keyHigh : &zone->velHigh;

                *low  = (lo > *low) ? lo : *low;
                *high = (hi < *high) ? hi : *high;
            }
            w->windows++;
        }
        p += 8u + n + (n & 1u);
    }
}

static bool bytes_contain(const uint8_t * d, uint32_t length, const char * tag) {
    for (uint32_t i = 0; (i + 4u) <= length; i++) {
        if (memcmp(d + i, tag, 4) == 0) {
            return true;
        }
    }

    return false;
}

// notes §2: UTF-16 in the Phdr, kept as ASCII
static void preset_name(const uint8_t * body, uint32_t length, uint32_t number, char * out, size_t size) {
    uint32_t at = 0;
    size_t   n  = 0;

    while (((at + 4u) <= length) && (memcmp(body + at, "Phdr", 4) != 0)) {
        at++;
    }

    for (uint32_t p = at + PHDR_NAME; ((at + 4u) <= length) && ((p + 1u) < length) && ((n + 1u) < size); p += 2u) {
        uint32_t c = (uint32_t)body[p] | ((uint32_t)body[p + 1] << 8);

        if (c == 0u) {
            break;
        }
        out[n++] = ((c >= 32u) && (c < 127u)) ? (char)c : '?';
    }

    while ((n > 0u) && (out[n - 1u] == ' ')) {
        n--;
    }
    out[n] = '\0';

    if (n == 0u) {
        snprintf(out, size, "Preset %u", (unsigned)(number + 1u));
    }
}

// ---- E-mu EOS banks (.E4B) - notes §8 -------------------------------------------------------------------------

#define E4_PRESET_VOICES      (0x12)   // in an E4P1 body, after its 2-byte index
#define E4_PRESET_HEADER      (0x52)
#define E4_VOICE_HEADER       (0x11c)
#define E4_ZONE_BYTES         (22)
#define E4_VOICE_KEYS         (0x0c)   // low at +0, high at +3; velocities at 0x10 and 0x13
#define E4_VOICE_TRANSPOSE    (0x20)   // then coarse tune, then fine tune
#define E4_VOICE_AMP_ENV      (0x6c)   // six stages of a rate and a level, bytes
#define E4_ZONE_SAMPLE        (0x08)
#define E4_ZONE_FINE          (0x0a)   // 1/64 semitone, big-endian signed
#define E4_ZONE_ROOT          (0x0c)
#define E4_SAMPLE_START_L     (0x14)   // little-endian byte offsets from the sample's name: start L and R, end L and R,
#define E4_SAMPLE_LOOP_L      (0x24)   // loop start L and R, loop end L and R
#define E4_SAMPLE_RATE        (0x34)
#define E4_SAMPLE_PITCH       (0x38)   // the sample's pitch word, 1/64 semitone; untuned: trunc(768 log2(rate / 44053))
#define E4_SAMPLE_FLAGS       (0x3a)
#define E4_FLAG_LOOP          (0x0001)
#define E4_FLAG_LEFT          (0x0020)
#define E4_FLAG_RIGHT         (0x0040)
#define E4_PITCH_REFERENCE    (44053.0)
#define E4_RATE_SCALE         (254.0)  // notes §8: a rate handed on as Emulator X stores it, 0.5 for 127

static uint16_t read_u16_be(const uint8_t * p) {
    return (uint16_t)(((uint32_t)p[0] << 8) | p[1]);
}

static uint16_t read_u16_le(const uint8_t * p) {
    return (uint16_t)(p[0] | ((uint32_t)p[1] << 8));
}

static float e4_channel(const uint8_t * body, uint32_t length, uint32_t at) {
    return (at + 1u < length) ? (float)(int16_t)read_u16_le(body + at) / 32768.0f : 0.0f;
}

// notes §8: one E3S1 sample - 16-bit little-endian frames, each channel a block of its own
static tEmuSample * e4_sample(const tEmuBank * bank, uint32_t number) {
    const uint8_t * b      = bank->data + bank->sampleBody[number];
    uint32_t        length = bank->sampleLength[number];

    if (length < E4_SAMPLE_FLAGS + 2u) {
        return NULL;
    }
    uint16_t        flags  = read_u16_le(b + E4_SAMPLE_FLAGS);
    bool            left   = (flags & E4_FLAG_LEFT) != 0u;
    bool            right  = (flags & E4_FLAG_RIGHT) != 0u;
    uint32_t        start  = read_u32_le(b + E4_SAMPLE_START_L + (left ? 0u : 4u));
    uint32_t        end    = read_u32_le(b + E4_SAMPLE_START_L + 8u + (left ? 0u : 4u));

    if ((end <= start) || (end > length)) {
        return NULL;
    }
    tEmuSample *    s      = calloc(1, sizeof(tEmuSample));

    if (s == NULL) {
        return NULL;
    }
    s->channels  = (left && right) ? 2u : 1u;
    s->frames    = (end - start) / 2u;
    s->data[0]   = malloc(sizeof(float) * s->frames);
    s->data[1]   = (s->channels == 2u) ? malloc(sizeof(float) * s->frames) : s->data[0];

    if ((s->data[0] == NULL) || (s->data[1] == NULL)) {
        sample_free(s);
        return NULL;
    }
    uint32_t        startR = read_u32_le(b + E4_SAMPLE_START_L + 4u);

    for (uint32_t i = 0; i < s->frames; i++) {
        s->data[0][i] = e4_channel(b, length, start + (2u * i));

        if (s->channels == 2u) {
            s->data[1][i] = e4_channel(b, length, startR + (2u * i));
        }
    }

    uint32_t        loopS  = read_u32_le(b + E4_SAMPLE_LOOP_L + (left ? 0u : 4u));
    uint32_t        loopE  = read_u32_le(b + E4_SAMPLE_LOOP_L + 8u + (left ? 0u : 4u));

    s->looped    = ((flags & E4_FLAG_LOOP) != 0u) && (loopS >= start) && (loopE > loopS) && (loopE <= end);
    s->loopStart = s->looped ? (loopS - start) / 2u : 0u;
    s->loopEnd   = s->looped ? (loopE - start) / 2u : s->frames;
    // notes §8: the recorded rate, retuned by whatever the pitch word holds beyond the one written for that rate
    double          rate   = (double)read_u32_le(b + E4_SAMPLE_RATE);
    int32_t         plain  = (rate > 0.0) ? (int32_t)(768.0 * log2(rate / E4_PITCH_REFERENCE)) : 0;
    int32_t         pitch  = (int16_t)read_u16_le(b + E4_SAMPLE_PITCH);

    s->rate      = ((rate > 1000.0) ? rate : (E4_PITCH_REFERENCE * pow(2.0, pitch / 768.0))) * ((rate > 1000.0) ? pow(2.0, (pitch - plain) / 768.0) : 1.0);
    s->number    = number;
    return s;
}

static const tEmuSample * e4_pool_sample(tEmuBank * bank, uint32_t number) {
    if ((number > MAX_SAMPLES) || (bank->sampleLength[number] == 0u)) {
        return NULL;
    }

    if (!bank->poolTried[number]) {
        bank->poolTried[number] = true;
        bank->pool[number]      = e4_sample(bank, number);
    }
    return bank->pool[number];
}

static int32_t clamp_i(int32_t v, int32_t lo, int32_t hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

// notes §8: a preset's voices, each a header and its zones; a zone plays within its own and its voice's ranges
static void e4_preset(tEmuBank * bank, const uint8_t * body, uint32_t length, tEmuPreset * out) {
    uint32_t voices = (length > E4_PRESET_VOICES + 2u) ? read_u16_be(body + E4_PRESET_VOICES) : 0u;
    uint32_t pos    = E4_PRESET_HEADER;

    for (uint32_t v = 0; (v < voices) && (pos + E4_VOICE_HEADER <= length); v++) {
        const uint8_t * vh    = body + pos;
        uint32_t        size  = read_u16_be(vh);
        uint32_t        zones = vh[2];
        tEmuEnvelope    amp   = {.present = true};
        double          tune  = (double)(int8_t)vh[E4_VOICE_TRANSPOSE] + (double)(int8_t)vh[E4_VOICE_TRANSPOSE + 1]
                                + ((double)(int8_t)vh[E4_VOICE_TRANSPOSE + 2] / 64.0);

        for (int k = 0; k < EMU_ENV_STAGES; k++) {
            amp.time[k]  = (float)(vh[E4_VOICE_AMP_ENV + (2 * k)] / E4_RATE_SCALE);
            amp.level[k] = (float)floor((vh[E4_VOICE_AMP_ENV + (2 * k) + 1] & 0x7F) * 100.0 / 127.0 + 0.5);
        }

        for (uint32_t z = 0; (z < zones) && (out->zoneCount < EMU_BANK_MAX_ZONES); z++) {
            const uint8_t *    zb     = vh + E4_VOICE_HEADER + (E4_ZONE_BYTES * z);

            if ((pos + E4_VOICE_HEADER + (E4_ZONE_BYTES * (z + 1u))) > length) {
                break;
            }
            const tEmuSample * sample = e4_pool_sample(bank, read_u16_be(zb + E4_ZONE_SAMPLE));

            if (sample == NULL) {
                continue;
            }
            tEmuZone *         zone   = &out->zone[out->zoneCount++];

            zone->sample   = sample;
            zone->rootNote = zb[E4_ZONE_ROOT];
            zone->keyLow   = clamp_i(zb[0], vh[E4_VOICE_KEYS], 127);
            zone->keyHigh  = clamp_i(zb[3], 0, vh[E4_VOICE_KEYS + 3]);
            zone->velLow   = clamp_i(zb[4], vh[E4_VOICE_KEYS + 4], 127);
            zone->velHigh  = clamp_i(zb[7], 0, vh[E4_VOICE_KEYS + 7]);
            zone->voice    = v;
            zone->tune     = tune + ((double)(int16_t)read_u16_be(zb + E4_ZONE_FINE) / 64.0);
            zone->amp      = amp;
        }

        if (size < E4_VOICE_HEADER) {
            break;
        }
        pos += size;
    }
}

// notes §2: a preset counts when one of its voices holds a zone - some banks carry presets of text alone
static bool e4_has_zones(const uint8_t * body, uint32_t length) {
    uint32_t voices = read_u16_be(body + E4_PRESET_VOICES);
    uint32_t pos    = E4_PRESET_HEADER;

    for (uint32_t v = 0; (v < voices) && ((pos + E4_VOICE_HEADER) <= length); v++) {
        uint32_t size = read_u16_be(body + pos);

        if (body[pos + 2] > 0u) {
            return true;
        }

        if (size < E4_VOICE_HEADER) {
            break;
        }
        pos += size;
    }

    return false;
}

// one chunk: its type at `p`, a length counting the 2-byte index, the index, the body
static void e4_chunk(tEmuBank * bank, const uint8_t * d, uint32_t size, uint32_t p) {
    if ((p + 10u) > size) {
        return;
    }
    uint32_t n     = read_u32_be(d + p + 4);
    uint32_t index = read_u16_be(d + p + 8);
    uint32_t body  = p + 10u;

    if (n < 2u) {
        return;
    }

    if (n > (size - p - 8u)) {   // notes §8: some converters count the chunk's header in its length - the last overruns
        n = size - p - 8u;

        if (n < 2u) {
            return;
        }
    }

    if ((memcmp(d + p, "E3S1", 4) == 0) && (index <= MAX_SAMPLES)) {
        bank->sampleBody[index]   = body;
        bank->sampleLength[index] = n - 2u;
    } else if (  (memcmp(d + p, "E4P1", 4) == 0) && (bank->presetCount < EMU_BANK_MAX_PRESETS) && (n - 2u > E4_PRESET_HEADER)
              && e4_has_zones(d + body, n - 2u)) {
        uint32_t i = bank->presetCount++;
        uint32_t k = 16;

        bank->presetBody[i]   = body;
        bank->presetLength[i] = n - 2u;

        while ((k > 0u) && ((d[body + k - 1u] == ' ') || (d[body + k - 1u] == '\0'))) {
            k--;
        }
        snprintf(bank->presetName[i], sizeof(bank->presetName[i]), "%.*s", (int)k, (const char *)(d + body));
    }
}

// notes §8: the chunks from the table of contents when the bank has one (an entry whose offset does not land on its
// own type is stale and passed over), else by walking them as older banks need
static bool e4_open(tEmuBank * bank, const uint8_t * d, uint32_t size) {
    if ((size >= 20u) && (memcmp(d + 12, "TOC1", 4) == 0)) {
        uint32_t toc = read_u32_be(d + 16);

        for (uint32_t k = 0; (k < (toc / 32u)) && ((20u + (32u * (k + 1u))) <= size); k++) {
            const uint8_t * e  = d + 20u + (32u * k);
            uint32_t        at = read_u32_be(e + 8);

            if (((at + 4u) <= size) && (memcmp(d + at, e, 4) == 0)) {
                e4_chunk(bank, d, size, at);
            }
        }
    } else {
        for (uint32_t p = 12; (p + 10u) <= size;) {
            uint32_t n = read_u32_be(d + p + 4);

            if ((n < 2u) || (n > (size - p - 8u))) {
                break;
            }
            e4_chunk(bank, d, size, p);
            p += 8u + n + (n & 1u);
        }
    }
    // notes §2: and one of its zones names a sample the bank holds
    uint32_t kept = 0;

    for (uint32_t i = 0; i < bank->presetCount; i++) {
        const uint8_t * body   = d + bank->presetBody[i];
        uint32_t        length = bank->presetLength[i];
        uint32_t        voices = read_u16_be(body + E4_PRESET_VOICES);
        bool            any    = false;

        for (uint32_t v = 0, pos = E4_PRESET_HEADER; (v < voices) && !any && ((pos + E4_VOICE_HEADER) <= length); v++) {
            uint32_t size = read_u16_be(body + pos);

            for (uint32_t z = 0; (z < body[pos + 2]) && ((pos + E4_VOICE_HEADER + (E4_ZONE_BYTES * (z + 1u))) <= length); z++) {
                uint32_t number = read_u16_be(body + pos + E4_VOICE_HEADER + (E4_ZONE_BYTES * z) + E4_ZONE_SAMPLE);

                any = any || ((number <= MAX_SAMPLES) && (bank->sampleLength[number] > 0u));
            }

            if (size < E4_VOICE_HEADER) {
                break;
            }
            pos += size;
        }

        if (any) {
            bank->presetBody[kept]   = bank->presetBody[i];
            bank->presetLength[kept] = bank->presetLength[i];
            memmove(bank->presetName[kept], bank->presetName[i], sizeof(bank->presetName[i]));
            kept++;
        }
    }

    bank->presetCount = kept;
    bank->e4b         = true;
    return bank->presetCount > 0u;
}

bool emu_bank_is_bank_file(const char * name) {
    const char * dot = strrchr(name, '.');

    return (dot != NULL) && ((strcasecmp(dot, ".exb") == 0) || (strcasecmp(dot, ".e4b") == 0));
}

// notes §2: the presets that hold zones; the empty ones a bank carries are passed over
// notes §6: Emulator X keeps each bank as a folder named like it, holding the bank and its SamplePool - given the
// folder, the bank inside it
static void bank_file(const char * path, char * out, size_t size) {
    struct stat  st;
    size_t       n    = strlen(path);

    while ((n > 1) && ((path[n - 1] == '/') || (path[n - 1] == '\\'))) {
        n--;
    }
    const char * leaf = path + n;

    while ((leaf > path) && (leaf[-1] != '/') && (leaf[-1] != '\\')) {
        leaf--;
    }

    if ((stat(path, &st) == 0) && S_ISDIR(st.st_mode)) {
        snprintf(out, size, "%.*s/%.*s", (int)n, path, (int)(path + n - leaf), leaf);
    } else {
        snprintf(out, size, "%s", path);
    }
}

tEmuBank * emu_bank_open(const char * given, char * error, size_t errorLength) {
    char       path[2048];

    bank_file(given, path, sizeof(path));
    tEmuBank * bank = calloc(1, sizeof(tEmuBank));
    uint32_t   size = 0;
    uint8_t *  d    = (bank != NULL) ? read_whole_file(path, &size) : NULL;

    if ((d != NULL) && (size >= 12u) && (memcmp(d, "FORM", 4) == 0) && (memcmp(d + 8, "E4B0", 4) == 0)) {   // notes §8
        snprintf(bank->path, sizeof(bank->path), "%s", path);
        bank->data = d;
        bank->size = size;

        if (!e4_open(bank, d, size)) {
            snprintf(error, errorLength, "The bank holds no presets with samples.");
            emu_bank_free(bank);
            return NULL;
        }
        return bank;
    }

    if ((d == NULL) || (size < 12u) || (memcmp(d, "FORM", 4) != 0) || (memcmp(d + 8, "E5B0", 4) != 0)) {
        snprintf(error, errorLength, "%s", (d == NULL) ? "The file could not be read." : "Not an Emulator X or EOS bank.");
        free(d);
        free(bank);
        return NULL;
    }
    snprintf(bank->path, sizeof(bank->path), "%s", path);

    for (uint32_t p = 12; ((p + 8u) <= size) && (bank->presetCount < EMU_BANK_MAX_PRESETS);) {
        uint32_t n = read_u32_be(d + p + 4);

        if (n > (size - p - 8u)) {
            break;
        }

        if ((memcmp(d + p, "E5P1", 4) == 0) && (n >= 2u) && bytes_contain(d + p + 8u, n, "Zhdr")) {
            uint32_t i = bank->presetCount++;

            bank->presetBody[i]   = p + 8u;
            bank->presetLength[i] = n;
            preset_name(d + p + 8u, n, i, bank->presetName[i], sizeof(bank->presetName[i]));
        }
        p += 8u + n + (n & 1u);
    }

    bank->data = d;
    bank->size = size;

    if (bank->presetCount == 0u) {
        snprintf(error, errorLength, "The bank holds no presets with samples.");
        emu_bank_free(bank);
        return NULL;
    }
    return bank;
}

void emu_bank_free(tEmuBank * bank) {
    if (bank == NULL) {
        return;
    }

    for (uint32_t i = 0; i <= MAX_SAMPLES; i++) {
        sample_free(bank->pool[i]);
    }

    free(bank->data);
    free(bank);
}

uint32_t emu_bank_preset_count(const tEmuBank * bank) {
    return (bank != NULL) ? bank->presetCount : 0u;
}

const char * emu_bank_preset_name(const tEmuBank * bank, uint32_t preset) {
    return ((bank != NULL) && (preset < bank->presetCount)) ? bank->presetName[preset] : "";
}

const char * emu_bank_path(const tEmuBank * bank) {
    return (bank != NULL) ? bank->path : "";
}

bool emu_bank_preset(tEmuBank * bank, uint32_t preset, tEmuPreset * out) {
    tWalk w;

    memset(out, 0, sizeof(*out));

    if ((bank == NULL) || (preset >= bank->presetCount)) {
        return false;
    }

    if (bank->e4b) {
        e4_preset(bank, bank->data + bank->presetBody[preset], bank->presetLength[preset], out);
        return out->zoneCount > 0u;
    }
    memset(&w, 0, sizeof(w));
    w.bank   = bank;
    w.preset = out;
    w.zone   = -1;
    walk(&w, bank->data, bank->presetBody[preset] + 2u, bank->presetBody[preset] + bank->presetLength[preset]);
    return out->zoneCount > 0u;
}
