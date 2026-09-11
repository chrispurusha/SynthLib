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

#ifndef __SYNTHLIB_MIDI_H__
#define __SYNTHLIB_MIDI_H__

// THE COREMIDI SEND PRIMITIVE, ONCE.
//
// EmuUtility and SynthEdit each carried their own midi_send_to(): the same MIDIPacketListInit /
// MIDIPacketListAdd / MIDISend over the same out port under the same mutex, character-identical
// apart from a log string. They differed in exactly two ways, and BOTH of those differences were
// SynthEdit having already been bitten:
//
//   * THE BUFFER. EmuUtility packs into 512 bytes of stack. SynthEdit used to as well, which was
//     ample for everything it sends EXCEPT a whole-bank restore — 18734 bytes in a real capture.
//     Past 512, MIDIPacketListAdd simply fails.
//   * THE RETURN VALUE. EmuUtility's returns void, as SynthEdit's did. That is what turned the
//     failure above into a lie: the send logged an error, but the caller had nothing to test, so
//     Restore Bank reported success while nothing had reached the wire. Found 2026-07-11 from the
//     owner's own MIDI Monitor capture showing no traffic at all.
//
// So this is not a tidy-up that happens to remove duplication — EmuUtility today has the 512-byte
// limit AND no way to notice it, which is the identical bug waiting for a large enough message.
// Sharing the one that learned fixes it there for free.
//
// WHAT IS DELIBERATELY NOT HERE: scan, connect and dispatch. Those have diverged between the two
// apps for real reasons — SynthEdit's is a multi-device framework with deferred identity replies,
// three destination-matching fallbacks and per-source running-status parsing; EmuUtility's is a
// single-device E-mu client — and merging them mechanically would be a bad trade. This is the safe
// piece: the bytes-to-the-wire primitive, which is the same everywhere.
//
// This header includes CoreMIDI, as synthlibWindow.h includes GLFW and for the same reason: the
// thing it describes IS CoreMIDI. Nothing platform-free links it.

#include <CoreMIDI/CoreMIDI.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The port everything is sent through. Call once, straight after MIDIOutputPortCreate() succeeds;
// pass 0 when tearing the client down, and sends become no-ops rather than touching a stale port.
//
// The port stays the application's to CREATE — naming it is the app's business ("EmuUtility Out",
// "SynthEdit Out") and creation sits inside the connect logic that is staying put.
void synthlib_midi_set_out_port(MIDIPortRef port);

// Sends one message. Returns false and logs if it could not be packed or MIDISend failed — CHECK IT
// for anything whose success is reported to the user; see the note above about Restore Bank.
//
// Serialised internally, so callers on different threads need no lock of their own. Messages up to
// SYNTHLIB_MIDI_MAX_MESSAGE bytes are packed from a shared buffer rather than the stack, so a bank
// restore does not need a 64KB stack frame at every call site that never sends one.
#define SYNTHLIB_MIDI_MAX_MESSAGE    (65536)

bool synthlib_midi_send_to(const uint8_t * data, uint32_t length, MIDIEndpointRef dest);

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

#endif // __SYNTHLIB_MIDI_H__
