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
// Notes: Docs/code-notes/synthlibMidi.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_MIDI_H__
#define __SYNTHLIB_MIDI_H__

// notes §1

#include <CoreMIDI/CoreMIDI.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// notes §2
void synthlib_midi_set_out_port(MIDIPortRef port);

// notes §3
#define SYNTHLIB_MIDI_MAX_MESSAGE    (65536)

bool synthlib_midi_send_to(const uint8_t * data, uint32_t length, MIDIEndpointRef dest);

#ifdef __cplusplus
}
#endif

// The port choice and the port lists, declared here too so that everything which included this for
// them still finds them. DEFINED in synthlibMidiPorts.c, not in synthlibMidi.c - see that header.
#include "synthlibMidiPorts.h"

#endif // __SYNTHLIB_MIDI_H__
