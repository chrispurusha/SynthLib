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

void synthlib_plugin_param_edited(void * inst, uint32_t id, double normalized) {
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
