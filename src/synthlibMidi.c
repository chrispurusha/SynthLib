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

// See synthlibMidi.h for what this is and which of the two apps' versions it inherited from.

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <pthread.h>

#include "synthlibMidi.h"
#include "synthlibDefs.h"

static MIDIPortRef     gOutPort   = 0;
static pthread_mutex_t gSendMutex = PTHREAD_MUTEX_INITIALIZER;

// ONE SHARED PACKING BUFFER RATHER THAN A STACK ONE. It has to be big enough for the largest thing
// any app sends — a whole-bank restore, ~18.7KB measured — and putting that on the stack would mean
// a 64KB frame at every call site, including the ones that only ever send three bytes. It is safe to
// share because the mutex below already serialises sends: the lock is taken before the buffer is
// touched and released after MIDISend has copied out of it.
static uint8_t         gPacketBuf[SYNTHLIB_MIDI_MAX_MESSAGE + sizeof(MIDIPacketList)];

void synthlib_midi_set_out_port(MIDIPortRef port) {
    gOutPort = port;
}

bool synthlib_midi_send_to(const uint8_t * data, uint32_t length, MIDIEndpointRef dest) {
    if ((gOutPort == 0) || (dest == 0) || (data == NULL) || (length == 0)) {
        return false;
    }

    if (length > SYNTHLIB_MIDI_MAX_MESSAGE) {
        LOG_ERROR("MIDI message of %u bytes exceeds the %u byte maximum, not sent\n",
                  (unsigned)length, (unsigned)SYNTHLIB_MIDI_MAX_MESSAGE);
        return false;
    }
    pthread_mutex_lock(&gSendMutex);

    MIDIPacketList * pktList = (MIDIPacketList *)gPacketBuf;
    MIDIPacket *     pkt     = MIDIPacketListInit(pktList);
    bool             ok      = false;

    pkt = MIDIPacketListAdd(pktList, sizeof(gPacketBuf), pkt, 0, length, data);

    if (pkt == NULL) {
        LOG_ERROR("MIDIPacketListAdd failed (message too long? %u bytes)\n", (unsigned)length);
    } else {
        OSStatus err = MIDISend(gOutPort, dest, pktList);

        if (err != noErr) {
            LOG_ERROR("MIDISend error %d\n", (int)err);
        } else {
            ok = true;
        }
    }
    pthread_mutex_unlock(&gSendMutex);

    return ok;
}

#ifdef __cplusplus
}
#endif
