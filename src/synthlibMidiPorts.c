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

// See synthlibMidiPorts.h - and synthlibMidi.h, where the port choice is described.

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include "synthlibMidiPorts.h"
#include "prefs.h"

// ── The chosen ports ─────────────────────────────────────────────────────────────────────────────
//
// Written on the UI thread and read by the MIDI thread at every scan, so the three strings are only
// ever touched under this lock. The prefs file is the UI thread's alone: it is read and written here
// only from the two UI-thread entry points, never from synthlib_midi_ports_chosen().

static pthread_mutex_t gChoiceMutex = PTHREAD_MUTEX_INITIALIZER;
static char            gChoiceScope[SYNTHLIB_MIDI_PORT_NAME_MAX];
static char            gChoiceInput[SYNTHLIB_MIDI_PORT_NAME_MAX];
static char            gChoiceOutput[SYNTHLIB_MIDI_PORT_NAME_MAX];

static void choice_key(char * out, size_t size, const char * base, const char * scope) {
    if (scope[0] == '\0') {
        snprintf(out, size, "%s", base);
    } else {
        snprintf(out, size, "%s.%s", base, scope);
    }
}

void synthlib_midi_ports_set_scope(const char * scope) {
    char         scopeCopy[SYNTHLIB_MIDI_PORT_NAME_MAX]   = {0};
    char         keyIn[SYNTHLIB_MIDI_PORT_NAME_MAX + 16]  = {0};
    char         keyOut[SYNTHLIB_MIDI_PORT_NAME_MAX + 16] = {0};
    const char * input                                    = NULL;
    const char * output                                   = NULL;

    snprintf(scopeCopy, sizeof(scopeCopy), "%s", (scope != NULL) ? scope : "");
    choice_key(keyIn, sizeof(keyIn), "midiInput", scopeCopy);
    choice_key(keyOut, sizeof(keyOut), "midiOutput", scopeCopy);
    input  = prefs_get_string(keyIn, "");
    output = prefs_get_string(keyOut, "");

    pthread_mutex_lock(&gChoiceMutex);
    snprintf(gChoiceScope, sizeof(gChoiceScope), "%s", scopeCopy);
    snprintf(gChoiceInput, sizeof(gChoiceInput), "%s", (input != NULL) ? input : "");
    snprintf(gChoiceOutput, sizeof(gChoiceOutput), "%s", (output != NULL) ? output : "");
    pthread_mutex_unlock(&gChoiceMutex);
}

void synthlib_midi_ports_choose(const char * input, const char * output) {
    char scope[SYNTHLIB_MIDI_PORT_NAME_MAX]       = {0};
    char keyIn[SYNTHLIB_MIDI_PORT_NAME_MAX + 16]  = {0};
    char keyOut[SYNTHLIB_MIDI_PORT_NAME_MAX + 16] = {0};

    pthread_mutex_lock(&gChoiceMutex);
    snprintf(gChoiceInput, sizeof(gChoiceInput), "%s", (input != NULL) ? input : "");
    snprintf(gChoiceOutput, sizeof(gChoiceOutput), "%s", (output != NULL) ? output : "");
    snprintf(scope, sizeof(scope), "%s", gChoiceScope);
    pthread_mutex_unlock(&gChoiceMutex);

    choice_key(keyIn, sizeof(keyIn), "midiInput", scope);
    choice_key(keyOut, sizeof(keyOut), "midiOutput", scope);
    prefs_set_string(keyIn, (input != NULL) ? input : "");
    prefs_set_string(keyOut, (output != NULL) ? output : "");
}

void synthlib_midi_ports_chosen(char * input, size_t inputSize, char * output, size_t outputSize) {
    pthread_mutex_lock(&gChoiceMutex);

    if ((input != NULL) && (inputSize > 0)) {
        snprintf(input, inputSize, "%s", gChoiceInput);
    }

    if ((output != NULL) && (outputSize > 0)) {
        snprintf(output, outputSize, "%s", gChoiceOutput);
    }
    pthread_mutex_unlock(&gChoiceMutex);
}

// ── The ports present ────────────────────────────────────────────────────────────────────────────

void synthlib_midi_port_name(MIDIEndpointRef endpoint, char * out, size_t size) {
    CFStringRef name = NULL;

    if ((out == NULL) || (size == 0)) {
        return;
    }
    out[0] = '\0';

    if (endpoint == 0) {
        return;
    }

    if ((MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &name) != noErr) || (name == NULL)) {
        name = NULL;
        (void)MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &name);
    }

    if (name != NULL) {
        if (!CFStringGetCString(name, out, (CFIndex)size, kCFStringEncodingUTF8)) {
            out[0] = '\0';
        }
        CFRelease(name);
    }
}

uint32_t synthlib_midi_port_names(bool inputs, char names[][SYNTHLIB_MIDI_PORT_NAME_MAX], uint32_t max) {
    ItemCount count   = inputs ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();
    uint32_t  written = 0;

    for (ItemCount i = 0; (i < count) && (written < max); i++) {
        MIDIEndpointRef endpoint = inputs ? MIDIGetSource(i) : MIDIGetDestination(i);

        synthlib_midi_port_name(endpoint, names[written], SYNTHLIB_MIDI_PORT_NAME_MAX);

        if (names[written][0] != '\0') {
            written++;
        }
    }

    return written;
}

MIDIEndpointRef synthlib_midi_find_port(bool input, const char * name) {
    ItemCount count = input ? MIDIGetNumberOfSources() : MIDIGetNumberOfDestinations();

    if ((name == NULL) || (name[0] == '\0')) {
        return 0;
    }

    for (ItemCount i = 0; i < count; i++) {
        MIDIEndpointRef endpoint                          = input ? MIDIGetSource(i) : MIDIGetDestination(i);
        char            here[SYNTHLIB_MIDI_PORT_NAME_MAX] = {0};

        synthlib_midi_port_name(endpoint, here, sizeof(here));

        if (strcmp(here, name) == 0) {
            return endpoint;
        }
    }

    return 0;
}

#ifdef __cplusplus
}
#endif
