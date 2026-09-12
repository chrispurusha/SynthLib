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
// Notes: Docs/code-notes/synthlibMidiPorts.h.md - "// notes §k" refers there.

#ifndef __SYNTHLIB_MIDI_PORTS_H__
#define __SYNTHLIB_MIDI_PORTS_H__

// notes §1

#include <CoreMIDI/CoreMIDI.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// notes §2
#define SYNTHLIB_MIDI_PORT_NAME_MAX    (128)

// UI thread. Loads the choice saved under `scope` (NULL or "" for the application-wide one) and makes
// it the current one. Call it before the MIDI thread's first scan, and again whenever the device
// the choice belongs to changes.
void synthlib_midi_ports_set_scope(const char * scope);

// UI thread. Records a new choice under the current scope and saves it. Either may be "" (automatic).
void synthlib_midi_ports_choose(const char * input, const char * output);

// Any thread: the MIDI thread consults it at every scan.
void synthlib_midi_ports_chosen(char * input, size_t inputSize, char * output, size_t outputSize);

// The ports present now, by name, in CoreMIDI's order. Returns how many were written.
uint32_t synthlib_midi_port_names(bool inputs, char names[][SYNTHLIB_MIDI_PORT_NAME_MAX], uint32_t max);

// The endpoint carrying exactly `name` now, or 0 if none does, i.e. the chosen port is unplugged.
MIDIEndpointRef synthlib_midi_find_port(bool input, const char * name);

// An endpoint's name as the lists show it: its display name, or its plain name if it has none.
void synthlib_midi_port_name(MIDIEndpointRef endpoint, char * out, size_t size);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_MIDI_PORTS_H__
