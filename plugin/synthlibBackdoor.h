/*
 * SynthLib - a plain-text command channel into a plug-in, for scripts and assistants.
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
// Notes: Docs/code-notes/synthlibBackdoor.md

#ifndef __SYNTHLIB_BACKDOOR_H__
#define __SYNTHLIB_BACKDOOR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "synthlibPlugin.h"

// What a plug-in lets the channel do; a NULL entry answers that the plug-in does not offer it
typedef struct {
    const tSynthLibParam * params;
    uint32_t               count;
    double (*get)(void * user, uint32_t id);
    void (*set)(void * user, uint32_t id, double normalized);       // as an edit on the panel
    bool (*text)(void * user, uint32_t id, double normalized, char * out, size_t len);
    const char * (*name)(void * user);
    void (*rename)(void * user, const char * name);
    bool (*preset)(void * user, int number);
    bool (*save)(void * user, const char * path);
    void (*init)(void * user);
} tSynthLibBackdoor;

// Call from the panel's sync. Does nothing until /tmp/<name>-backdoor exists.
void synthlib_backdoor_poll(const char * name, const tSynthLibBackdoor * ops, void * user);

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_BACKDOOR_H__
