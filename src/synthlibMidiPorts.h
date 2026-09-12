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

#ifndef __SYNTHLIB_MIDI_PORTS_H__
#define __SYNTHLIB_MIDI_PORTS_H__

// SPLIT OUT OF synthlibMidi.c ON 2026-09-11, THE DAY IT WENT IN. The choice is saved in prefs, and the
// send primitive is compiled by plug-ins that have no prefs to link against - GenBridge stopped
// linking the moment this landed beside it. The send primitive stays dependency-free; this file is
// for the applications, which get it automatically (SynthLib/src is a synchronized folder).

#include <CoreMIDI/CoreMIDI.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── WHICH PORTS THE USER CHOSE ──────────────────────────────────────────────────────────────────
//
// Both applications used to find their device only by broadcasting an identity request to every
// output and taking whichever input answered, then GUESSING the output from the input's entity. That
// is right on a single-cable rig and wrong on the owner's, where a synth is driven through one
// interface and answers through another (see find_dest_for_source() and the destination probe). A
// port the user picks settles it; the scan stays as the automatic choice.
//
// REMEMBERED BY NAME, because a MIDIEndpointRef does not survive a setup change or a relaunch and a
// name is what the user picked. "" means automatic. A chosen port that is not present is WAITED FOR,
// never swapped for another - the fall-through that sent GenBridge's notes to whatever came first.
//
// A SCOPE keeps separate choices for separate devices. SynthEdit plays a Z1 on one interface and a
// Voyager on another, so one choice per application would be wrong the moment it switched; it scopes
// by device configuration. EmuUtility has one device and uses the unscoped choice.
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
