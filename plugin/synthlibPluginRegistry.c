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

#include <stdatomic.h>
#include <stddef.h>
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <pthread.h>
#endif

#include "synthlibLog.h"
#include "synthlibPlugin.h"

static tSynthLibPluginVariantsFn     gVariantsFn = NULL;
static const tSynthLibPluginFormat * gFormat     = NULL;

void synthlib_plugin_register(tSynthLibPluginVariantsFn variantsFn, const char * logName) {
    gVariantsFn = variantsFn;
    synthlib_log_set_name(logName);
}

const tSynthLibPluginSet * synthlib_plugin_variants(void) {
    return gVariantsFn();
}

void synthlib_plugin_set_format(const tSynthLibPluginFormat * format) {
    gFormat = format;
}

// ---- edits from off the main thread - synthlibPlugin.h notes §29 ------------------------------------------

#if defined(__APPLE__)
#define EDIT_SLOTS    (8192u)   // a power of two; a whole sound from every instance in a large set

typedef struct {
    atomic_size_t turn;   // 2 x lap: free for that lap's writer; 2 x lap + 1: written, for its reader
    void *        inst;
    uint32_t      id;
    double        value;
} tEditSlot;

static tEditSlot     gEditSlots[EDIT_SLOTS];
static atomic_size_t gEditHead;           // the next slot a writer takes, from any thread
static size_t        gEditTail;           // the next slot to read - the main thread's alone
static atomic_bool   gEditDrainPosted;
static atomic_bool   gEditOverflowed;

static bool edit_push(void * inst, uint32_t id, double normalized) {
    size_t pos = atomic_load_explicit(&gEditHead, memory_order_relaxed);

    for (;;) {
        tEditSlot * slot = &gEditSlots[pos & (EDIT_SLOTS - 1u)];
        size_t      lap  = pos / EDIT_SLOTS;
        size_t      turn = atomic_load_explicit(&slot->turn, memory_order_acquire);

        if (turn == (2u * lap)) {
            if (atomic_compare_exchange_weak_explicit(&gEditHead, &pos, pos + 1u, memory_order_relaxed, memory_order_relaxed)) {
                slot->inst  = inst;
                slot->id    = id;
                slot->value = normalized;
                atomic_store_explicit(&slot->turn, (2u * lap) + 1u, memory_order_release);
                return true;
            }
        } else if (turn < (2u * lap)) {
            return false;   // full: the last lap's edit in this slot is not read yet
        } else {
            pos = atomic_load_explicit(&gEditHead, memory_order_relaxed);
        }
    }
}

// MAIN THREAD. The flag comes down before the reading starts, so an edit written after the last read
// posts a drain of its own
static void edit_drain(void * unused) {
    (void)unused;
    atomic_store(&gEditDrainPosted, false);

    for (;;) {
        tEditSlot * slot = &gEditSlots[gEditTail & (EDIT_SLOTS - 1u)];
        size_t      lap  = gEditTail / EDIT_SLOTS;

        if (atomic_load_explicit(&slot->turn, memory_order_acquire) != ((2u * lap) + 1u)) {
            break;
        }
        void *      inst  = slot->inst;
        uint32_t    id    = slot->id;
        double      value = slot->value;

        atomic_store_explicit(&slot->turn, 2u * (lap + 1u), memory_order_release);
        gEditTail++;
        gFormat->paramEdited(inst, id, value);
    }

    if (atomic_exchange(&gEditOverflowed, false)) {
        synthlib_log_line("parameter edits from the audio thread overflowed %u slots; some did not reach the host", EDIT_SLOTS);
    }
}
#endif

void synthlib_plugin_param_edited(void * inst, uint32_t id, double normalized) {
#if defined(__APPLE__)
    if (pthread_main_np() == 0) {
        if (!edit_push(inst, id, normalized)) {
            atomic_store(&gEditOverflowed, true);
        }

        if (!atomic_exchange(&gEditDrainPosted, true)) {
            dispatch_async_f(dispatch_get_main_queue(), NULL, edit_drain);
        }
        return;
    }
#endif
    gFormat->paramEdited(inst, id, normalized);
}

double synthlib_plugin_param_value(void * inst, uint32_t id) {
    return gFormat->paramValue(inst, id);
}

void synthlib_plugin_latency_changed(void * inst) {
    gFormat->latencyChanged(inst);
}

bool synthlib_plugin_request_resize(void * inst, double width, double height) {
    return gFormat->requestResize(inst, width, height);
}

bool synthlib_plugin_send_message(void * inst, const char * id, int64_t value) {
    return gFormat->sendMessage(inst, id, value);
}

bool synthlib_plugin_midi_out(void * inst, uint8_t status, uint8_t data1, uint8_t data2, uint32_t sampleOffset) {
    return gFormat->midiOut(inst, status, data1, data2, sampleOffset);
}
