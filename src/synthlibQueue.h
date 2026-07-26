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

#ifndef __SYNTHLIB_QUEUE_H__
#define __SYNTHLIB_QUEUE_H__

#include <pthread.h>
#include <stdint.h>
#include <stddef.h>

// A generic, payload-agnostic thread-safe FIFO for passing fixed-size messages between two threads
// (e.g. a UI thread and a device-comms thread). The queue copies payloadSize opaque bytes per message
// and never interprets them, so the embedding app defines its own message struct and passes
// sizeof(that struct) to msg_init(). One queue is fixed to one payload size.
//
// Typical use: one queue per direction (e.g. gToUsbThread / gToGuiThread). The consumer either blocks
// (eRcvWait — a dedicated worker thread) or polls (eRcvPoll — a render loop that must not block, then
// wakes itself / is woken to drain). The mechanism has no app dependencies, so it lives in SynthLib.

typedef enum {
    eRcvPoll,
    eRcvWait
} eRcv;

typedef struct _message {
    struct _message * nextMessage;
    uint8_t           payload[]; // flexible array member — payloadSize bytes follow the node
} tMessage;

typedef struct {
    pthread_mutex_t mutex;       // guards head/tail (msg_send()/msg_receive())
    pthread_mutex_t semMutex;    // guards semCount/semCond
    pthread_cond_t  semCond;
    int             semCount;    // pending-message count (condition-variable semaphore state)
    size_t          payloadSize; // bytes copied per message, fixed at msg_init()
    tMessage *      head;
    tMessage *      tail;
} tMessageQueue;

#ifdef __cplusplus
extern "C" {
#endif

// content buffers passed to msg_send/msg_receive must be at least payloadSize bytes (the size given
// to msg_init). semName is currently unused (the "semaphore" is a mutex+condvar counter) — kept for
// call-site clarity / future named-primitive use.
void msg_init(tMessageQueue * msgQueue, char * semName, size_t payloadSize);
int msg_receive(tMessageQueue * msgQueue, eRcv rcv, void * content);
void msg_send(tMessageQueue * msgQueue, const void * content);
int msg_count(tMessageQueue * msgQueue);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_QUEUE_H__
