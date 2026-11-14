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

#ifndef __EMU_BANK_H__
#define __EMU_BANK_H__

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define EMU_BANK_MAX_PRESETS        (256)   // notes §2: the ones holding zones
#define EMU_BANK_MAX_ZONES          (256)
#define EMU_BANK_PRESET_NAME_SIZE   (32)

// notes §3: one sample, a float array per channel; data[1] == data[0] when mono
typedef struct {
    float *  data[2];
    uint32_t channels;
    uint32_t frames;
    double   rate;          // the one to play at, its fine tuning included
    bool     looped;
    uint32_t loopStart;
    uint32_t loopEnd;       // exclusive
    uint32_t number;        // its SLnnn
} tEmuSample;

// notes §2: a sample mapped to keys and velocities
typedef struct {
    const tEmuSample * sample;   // owned by the bank
    int32_t            rootNote;
    int32_t            keyLow, keyHigh;
    int32_t            velLow, velHigh;
    uint32_t           voice;    // the preset's voice it belongs to, from 0
} tEmuZone;

typedef struct {
    uint32_t zoneCount;
    tEmuZone zone[EMU_BANK_MAX_ZONES];
} tEmuPreset;

typedef struct tEmuBank tEmuBank;

tEmuBank * emu_bank_open(const char * path, char * error, size_t errorLength);   // reads the bank, not its samples
void emu_bank_free(tEmuBank * bank);
bool emu_bank_is_bank_file(const char * name);                                   // by extension
uint32_t emu_bank_preset_count(const tEmuBank * bank);
const char * emu_bank_preset_name(const tEmuBank * bank, uint32_t preset);
const char * emu_bank_path(const tEmuBank * bank);
// notes §4: a preset's zones, reading the samples it names the first time any preset does; false past the last
// or when none of its samples can be read. Not for the audio thread.
bool emu_bank_preset(tEmuBank * bank, uint32_t preset, tEmuPreset * out);

#endif // __EMU_BANK_H__
