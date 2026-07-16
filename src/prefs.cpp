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

namespace {

std::string                         sAppName;
fs::path                            sFilePath;
std::map<std::string, std::string>  sValues;
bool                                sLoaded = false;
std::string                         sGetStringScratch; // Backing storage for prefs_get_string()'s returned pointer.

// Per-OS standard location for small app-preference files, one subfolder per app name so
// G2-Edit/Z1-Edit/EmuUtility (all built on this same SynthLib code) don't collide with each
// other's settings.
fs::path config_dir(const std::string & appName) {
#if defined(_WIN32)
    // TODO(Windows): %APPDATA% (C:\Users\<user>\AppData\Roaming) is the documented convention for
    // per-user roaming settings — this branch follows it, but is unverified: no SynthLib-based
    // app has a Windows build yet. Revisit (and actually build/run this) once one exists.
    const char * appData = std::getenv("APPDATA");
    fs::path     base    = (appData != nullptr) ? fs::path(appData) : fs::path(".");

    return base / appName;
#elif defined(__APPLE__)
    const char * home = std::getenv("HOME");
    fs::path     base = (home != nullptr) ? (fs::path(home) / "Library" / "Application Support") : fs::path(".");

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

void load_if_needed(void) {
    if (sLoaded) {
        return;
    }
    sLoaded = true;

    std::ifstream file(sFilePath);

    if (!file.is_open()) {
        return; // First run — no file yet, sValues stays empty.
    }
    std::string line;

    while (std::getline(file, line)) {
        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back(); // Tolerate a Windows-authored file being read back on another OS.
        }
        size_t eq = line.find('=');

        if (eq == std::string::npos) {
            continue;
        }
        sValues[line.substr(0, eq)] = line.substr(eq + 1);
    }
}

void save(void) {
    std::error_code ec;

    fs::create_directories(sFilePath.parent_path(), ec);

    std::ofstream file(sFilePath, std::ios::trunc);

    if (!file.is_open()) {
        return;
    }

    for (const auto & entry : sValues) {
        file << entry.first << "=" << entry.second << "\n";
    }
}

} // namespace

extern "C" {

void prefs_init(const char * appName) {
    sAppName  = (appName != nullptr) ? appName : "";
    sFilePath = config_dir(sAppName) / "prefs.txt";
    sLoaded   = false;
    sValues.clear();
    load_if_needed();
}

void prefs_set_string(const char * key, const char * value) {
    if ((key == nullptr) || (value == nullptr)) {
        return;
    }
    load_if_needed();
    sValues[key] = value;
    save();
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
    load_if_needed();
    return sValues.find(key) != sValues.end();
}

const char * prefs_get_string(const char * key, const char * defaultValue) {
    if (key == nullptr) {
        return defaultValue;
    }
    load_if_needed();

    auto it = sValues.find(key);

    if (it == sValues.end()) {
        return defaultValue;
    }
    sGetStringScratch = it->second;
    return sGetStringScratch.c_str();
}

double prefs_get_double(const char * key, double defaultValue) {
    if (key == nullptr) {
        return defaultValue;
    }
    load_if_needed();

    auto it = sValues.find(key);

    if (it == sValues.end()) {
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
    load_if_needed();

    auto it = sValues.find(key);

    if (it == sValues.end()) {
        return defaultValue;
    }
    char * endPtr = nullptr;
    long   result = std::strtol(it->second.c_str(), &endPtr, 10);

    return (endPtr == it->second.c_str()) ? defaultValue : result;
}

} // extern "C"
