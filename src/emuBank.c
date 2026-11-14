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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "emuBank.h"

#define MAX_SAMPLES        (1000)
#define EBL_HEADER_BASE    (2)         // notes §3: the sample header's offsets count from here in the E5S1 chunk
#define EBL_START_L        (0x48)      // then start R, end L, end R, loop start L and R, loop end L and R
#define EBL_RATE           (0x68)
#define EBL_OPTIONS        (0x6e)
#define EBL_OPTION_LOOP    (0x0001)
#define ZHDR_SAMPLE        (4)
#define ZHDR_ROOT          (10)
#define ETW_LOW            (4)
#define ETW_HIGH           (7)
#define PHDR_NAME          (12)        // UTF-16, from the Phdr tag

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
};

static uint32_t read_u32_be(const uint8_t * p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
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
        d = malloc((size_t)n);

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
    s->channels = stereo ? 2u : 1u;
    s->data[0]  = malloc(sizeof(float) * frames);
    s->data[1]  = stereo ? malloc(sizeof(float) * frames) : s->data[0];

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

    // notes §3: the stored loop end is the frame before the loop's last
    uint32_t        loopS  = (f[4] >= startL) ? ((f[4] - startL) / 2u) : 0u;
    uint32_t        loopE  = (f[6] >= startL) ? (((f[6] - startL) / 2u) + 2u) : 0u;

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

    w->zone = -1;

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
            walk(w, d, p + 8u, p + 8u + n);
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

bool emu_bank_is_bank_file(const char * name) {
    const char * dot = strrchr(name, '.');

    return (dot != NULL) && (strcasecmp(dot, ".exb") == 0);
}

// notes §2: the presets that hold zones; the empty ones a bank carries are passed over
tEmuBank * emu_bank_open(const char * path, char * error, size_t errorLength) {
    tEmuBank * bank = calloc(1, sizeof(tEmuBank));
    uint32_t   size = 0;
    uint8_t *  d    = (bank != NULL) ? read_whole_file(path, &size) : NULL;

    if ((d == NULL) || (size < 12u) || (memcmp(d, "FORM", 4) != 0) || (memcmp(d + 8, "E5B0", 4) != 0)) {
        snprintf(error, errorLength, "%s", (d == NULL) ? "The file could not be read." : "Not an Emulator X bank.");
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
    memset(&w, 0, sizeof(w));
    w.bank   = bank;
    w.preset = out;
    w.zone   = -1;
    walk(&w, bank->data, bank->presetBody[preset] + 2u, bank->presetBody[preset] + bank->presetLength[preset]);
    return out->zoneCount > 0u;
}
