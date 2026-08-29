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

#ifndef SYNTHLIB_VERSION_H
#define SYNTHLIB_VERSION_H

// ── Which build is this? ────────────────────────────────────────────────────────────────────────
//
// The question the plug-in made urgent: a .vst3 in a host's plug-in folder carries nothing that
// says where it came from, and "G2 Alike" looks the same whether it was built this morning or three
// weeks ago. An About box is the only place that can say.
//
// THE VERSION COMES FROM GIT, and only when a build system that knows about git supplies it.
// do-vst3 and do-release pass -DSYNTHLIB_VERSION_STRING from `git describe --tags`, which gives
// something like "V0.6.4-beta.5-9-g4fc085a": the last release tag, how many commits have landed
// since, and the commit itself. That is exactly enough to find the source a binary came from.
//
// A PLAIN XCODE BUILD SAYS SO INSTEAD. Xcode cannot run git without a Run Script phase, and the
// project's MARKETING_VERSION is stale and documented as not being the source of truth — reporting
// it would be worse than useless, because a wrong version number is believed. So an Xcode build
// says "development build" and leaves the timestamp to identify it.
#ifndef SYNTHLIB_VERSION_STRING
#define SYNTHLIB_VERSION_STRING    "development build"
#endif

// A block of text naming the application, its version, when it was compiled, and which render
// backend is running — that last is worth having now the backend is a preference: "it looks wrong"
// and "it looks wrong on Metal" are different reports.
//
// Returns a pointer to a static buffer, rebuilt on each call. Not thread-safe, which is no
// constraint on something a menu item shows.
const char * synthlib_about_text(const char * appName);

#endif // SYNTHLIB_VERSION_H
