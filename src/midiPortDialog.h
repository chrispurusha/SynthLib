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
// Notes: Docs/code-notes/midiPortDialog.h.md - "// notes §k" refers there.

#ifndef __MIDI_PORT_DIALOG_H__
#define __MIDI_PORT_DIALOG_H__

// notes §1

#include <stddef.h>
#include <stdbool.h>

#include "synthlibPopups.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char * title;                           // e.g. "MIDI Ports - Korg Z1"; copied on open
    void (*changed)(void);                        // the choice changed and has been saved: reconnect
    void (*scan)(void);                           // the Scan button
    void (*status)(char * text, size_t size);     // one line on the connection; NULL for none
} tMidiPortDialogHost;

// UI thread. Opens the dialogue on the current choice. The host struct is copied.
void midi_port_dialog_open(const tMidiPortDialogHost * host);

bool midi_port_dialog_active(void);

// The coordinator entry, for synthlib_popups_register(). Static storage, as that requires.
const tSynthLibPopup * midi_port_dialog_popup(void);

#ifdef __cplusplus
}
#endif

#endif // __MIDI_PORT_DIALOG_H__
