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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

#include "prefs.h"

namespace fs = std::filesystem;

namespace
{

std::string                        sAppName;
std::string                        sGetStringScratch;  // Backing storage for prefs_get_string()'s returned pointer.

// Two stores, same format, separate files. Settings (prefs.txt) are small and change when the user
// changes something; the patch-name cache (cache.txt) is bulky and is rewritten far more often —
// potentially on every reply during a name sweep. Keeping them apart means cache churn can never
// cost the user their settings, and lets the cache be deleted wholesale without touching prefs.
struct tStore {
    fs::path                           path;
    std::map<std::string, std::string> values;
    bool                               loaded = false;
};

tStore                             sPrefs;
tStore                             sCache;

// Per-OS standard location for small app-preference files, one subfolder per app name so
// G2-Edit/Z1-Edit/EmuUtility (all built on this same SynthLib code) don't collide with each
// other's settings.
fs::path config_dir(const std::string &appName) {
#if defined (_WIN32)
    // TODO(Windows): %APPDATA% (C:\Users\<user>\AppData\Roaming) is the documented convention for
    // per-user roaming settings — this branch follows it, but is unverified: no SynthLib-based
    // app has a Windows build yet. Revisit (and actually build/run this) once one exists.
    const char * appData   = std::getenv("APPDATA");
    fs::path     base      = (appData != nullptr) ? fs::path(appData) : fs::path(".");

    return base / appName;
#elif defined (__APPLE__)
    const char * home      = std::getenv("HOME");
    fs::path     base      = (home != nullptr) ? (fs::path(home) / "Library" / "Application Support") : fs::path(".");

    return base / appName;
#else
    // TODO(Linux): the XDG Base Directory spec ($XDG_CONFIG_HOME/<app>, falling back to
    // ~/.config/<app>) is the documented convention here — also unverified, no Linux build of any
    // SynthLib-based app exists yet. Revisit (and actually build/run this) once one does.
    const char * xdgConfig = std::getenv("XDG_CONFIG_HOME");
    fs::path     base;

    if ((xdgConfig != nullptr) && (xdgConfig[0] != '\0')) {
        base = fs::path(xdgConfig);
    } else {
        const char * home = std::getenv("HOME");
        base = (home != nullptr) ? (fs::path(home) / ".config") : fs::path(".");
    }
    return base / appName;
#endif
}

void load_if_needed(tStore &store) {
    if (store.loaded) {
        return;
    }
    store.loaded = true;

    std::ifstream file(store.path);

    if (!file.is_open()) {
        return; // First run — no file yet, store.values stays empty.
    }
    std::string   line;

    while (std::getline(file, line)) {
        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back(); // Tolerate a Windows-authored file being read back on another OS.
        }
        size_t eq = line.find('=');

        if (eq == std::string::npos) {
            continue;
        }
        store.values[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

// Written to a sibling temp file and renamed into place, rather than truncating the real file and
// rewriting it. rename() is atomic within a directory, so a crash or a kill mid-save leaves the
// previous file fully intact instead of a half-written one. This used to open the live file with
// std::ios::trunc, which put every saved setting at risk on every single write.
void save(tStore &store) {
    std::error_code ec;

    fs::create_directories(store.path.parent_path(), ec);

    fs::path        tempPath = store.path;

    tempPath += ".tmp";
    {
        std::ofstream file(tempPath, std::ios::trunc);

        if (!file.is_open()) {
            return;
        }

        for (const auto &entry : store.values) {
            file << entry.first << "=" << entry.second << "\n";
        }

        if (!file.good()) {
            file.close();
            fs::remove(tempPath, ec); // Partial write - discard it rather than renaming it over good data.
            return;
        }
    } // Closed (and flushed) before the rename - renaming an open stream's target is not safe.

    fs::rename(tempPath, store.path, ec);

    if (ec) {
        fs::remove(tempPath, ec);
    }
}

} // namespace

extern "C" {
void prefs_init(const char * appName) {
    sAppName      = (appName != nullptr) ? appName : "";

    sPrefs.path   = config_dir(sAppName) / "prefs.txt";
    sPrefs.loaded = false;
    sPrefs.values.clear();
    load_if_needed(sPrefs);

    // Alongside prefs.txt, same directory, same format - see tStore's own comment for why it is a
    // separate file rather than more keys in prefs.txt.
    sCache.path   = config_dir(sAppName) / "cache.txt";
    sCache.loaded = false;
    sCache.values.clear();
    load_if_needed(sCache);
}

void prefs_set_string(const char * key, const char * value) {
    if ((key == nullptr) || (value == nullptr)) {
        return;
    }
    load_if_needed(sPrefs);
    sPrefs.values[key] = value;
    save(sPrefs);
}

void prefs_set_double(const char * key, double value) {
    if (key == nullptr) {
        return;
    }
    prefs_set_string(key, std::to_string(value).c_str());
}

void prefs_set_int(const char * key, long value) {
    if (key == nullptr) {
        return;
    }
    prefs_set_string(key, std::to_string(value).c_str());
}

bool prefs_has_key(const char * key) {
    if (key == nullptr) {
        return false;
    }
    load_if_needed(sPrefs);
    return sPrefs.values.find(key) != sPrefs.values.end();
}

const char * prefs_get_string(const char * key, const char * defaultValue) {
    if (key == nullptr) {
        return defaultValue;
    }
    load_if_needed(sPrefs);

    auto it = sPrefs.values.find(key);

    if (it == sPrefs.values.end()) {
        return defaultValue;
    }
    sGetStringScratch = it->second;
    return sGetStringScratch.c_str();
}

double prefs_get_double(const char * key, double defaultValue) {
    if (key == nullptr) {
        return defaultValue;
    }
    load_if_needed(sPrefs);

    auto   it     = sPrefs.values.find(key);

    if (it == sPrefs.values.end()) {
        return defaultValue;
    }
    char * endPtr = nullptr;
    double result = std::strtod(it->second.c_str(), &endPtr);

    return (endPtr == it->second.c_str()) ? defaultValue : result;
}

long prefs_get_int(const char * key, long defaultValue) {
    if (key == nullptr) {
        return defaultValue;
    }
    load_if_needed(sPrefs);

    auto   it     = sPrefs.values.find(key);

    if (it == sPrefs.values.end()) {
        return defaultValue;
    }
    char * endPtr = nullptr;
    long   result = std::strtol(it->second.c_str(), &endPtr, 10);

    return (endPtr == it->second.c_str()) ? defaultValue : result;
}

// ── Patch-name cache ────────────────────────────────────────────────────────
// Same on-disk format as prefs, different file (cache.txt). Only strings are needed - the cache
// holds one packed blob per device - so there are deliberately no _int/_double twins.

void cache_set_string(const char * key, const char * value) {
    if ((key == nullptr) || (value == nullptr)) {
        return;
    }
    load_if_needed(sCache);
    sCache.values[key] = value;
    save(sCache);
}

// Falls back to prefs for a key the cache does not have, so caches written before the split are
// still found on first run after upgrading. The next cache_set_string() lands in cache.txt, after
// which the stale prefs.txt copy is simply ignored.
const char * cache_get_string(const char * key, const char * defaultValue) {
    if (key == nullptr) {
        return defaultValue;
    }
    load_if_needed(sCache);

    auto it = sCache.values.find(key);

    if (it == sCache.values.end()) {
        return prefs_get_string(key, defaultValue);
    }
    sGetStringScratch = it->second;
    return sGetStringScratch.c_str();
}

} // extern "C"
