/*
 * SynthLib - a plug-in's diagnostic log, switched on by a file.
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

#ifndef __SYNTHLIB_LOG_H__
#define __SYNTHLIB_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

// DIAGNOSTICS, GATED ON A FILE rather than an environment variable. The obvious gate would be getenv,
// and it does not work: a host launched from the Dock inherits no shell environment, so the variable
// is never seen in the one situation that matters. A file the user can touch is visible from anywhere.
//
//     touch /tmp/<name>-log        # on within a second, no reload
//     cat /tmp/<name>.log
//     rm /tmp/<name>-log           # off again
//
// The GATE is the hyphen and the OUTPUT is the dot - deliberately similar, and it has caught both
// sibling projects out once.
//
// THE PROJECT NAMES ITSELF by defining this, once, in its own plug-in source:
//
//     const char gSynthLibLogName[] = "genbridge";
//
// A constant rather than a set-the-name call, so there is no first call for two threads to race over
// and no line logged before the name was set. A build that compiles this file and forgets the
// definition fails to link rather than logging somewhere unexpected.
//
// EVERY LINE SAYS WHO WROTE IT - "[Live 1234] ..." - because one log file is shared by every instance
// in every process on the machine: a DAW with two plug-ins in it and a command line harness running
// alongside all append here. Reading it without attribution means diagnosing one process's symptom
// from another's output, which is exactly what happened in GenBridge.
//
// CALLABLE FROM ANY THREAD. With the gate absent - almost always - a call costs a clock read and two
// atomic loads; the gate itself, a syscall, is re-checked at most once a second. With it present each
// line is an fopen/fclose in append mode, so a line is one write and lines from concurrent callers do
// not interleave (plugin/test/logTest.c). Not for the audio thread with the gate present.
//
// Moved here on 2026-09-12 from GenBridge's gbLog.c, which MidiSyncTool had copied as msLog.c and
// which had since drifted: MidiSyncTool's checked the gate once per process and never again.
extern const char gSynthLibLogName[];

void synthlib_log_line(const char * format, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif // __SYNTHLIB_LOG_H__
