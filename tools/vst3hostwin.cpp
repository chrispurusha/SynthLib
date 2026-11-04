/*
 * SynthLib - vst3hostwin, a minimal VST3 test host for Windows.
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
// Notes: Docs/code-notes/vst3hostwin.cpp.md - "// notes §k" refers there.

// notes §1

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#include "miniaudio.h"
#pragma clang diagnostic pop

using namespace Steinberg;
using namespace Steinberg::Vst;

// The interface IDs come from the SDK's coreiids.cpp, vstinitiids.cpp and commoniids.cpp, linked beside this

#define MAX_BLOCK    (512)

// ------------------------------------------------------------------------------------------------
// The host's half of the VST3 objects
// ------------------------------------------------------------------------------------------------

#define NO_QUERY                                                                         \
    tresult PLUGIN_API queryInterface(const TUID, void ** o) override {                  \
        *o = nullptr;                                                                    \
        return kNoInterface;                                                             \
    }                                                                                    \
    uint32 PLUGIN_API addRef(void) override { return 1; }                                \
    uint32 PLUGIN_API release(void) override { return 1; }

// notes §2: what the wrapper's processor and controller say to each other through the host
class Attributes : public IAttributeList {
public:
    std::map<std::string, int64>             ints;
    std::map<std::string, double>            floats;
    std::map<std::string, std::u16string>    strings;
    std::map<std::string, std::vector<char>> blobs;
    NO_QUERY

    tresult PLUGIN_API setInt(AttrID id, int64 v) override { ints[id] = v; return kResultOk; }
    tresult PLUGIN_API getInt(AttrID id, int64 & v) override {
        auto it = ints.find(id);
        if (it == ints.end()) { return kResultFalse; }
        v = it->second;
        return kResultOk;
    }
    tresult PLUGIN_API setFloat(AttrID id, double v) override { floats[id] = v; return kResultOk; }
    tresult PLUGIN_API getFloat(AttrID id, double & v) override {
        auto it = floats.find(id);
        if (it == floats.end()) { return kResultFalse; }
        v = it->second;
        return kResultOk;
    }
    tresult PLUGIN_API setString(AttrID id, const TChar * s) override {
        strings[id] = (s != nullptr) ? std::u16string((const char16_t *)s) : std::u16string();
        return kResultOk;
    }
    tresult PLUGIN_API getString(AttrID id, TChar * s, uint32 bytes) override {
        auto it = strings.find(id);
        if ((it == strings.end()) || (bytes < sizeof(TChar))) { return kResultFalse; }
        size_t n = std::min(it->second.size(), (size_t)(bytes / sizeof(TChar)) - 1);
        memcpy(s, it->second.data(), n * sizeof(TChar));
        s[n] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API setBinary(AttrID id, const void * d, uint32 n) override {
        blobs[id] = std::vector<char>((const char *)d, (const char *)d + n);
        return kResultOk;
    }
    tresult PLUGIN_API getBinary(AttrID id, const void *& d, uint32 & n) override {
        auto it = blobs.find(id);
        if (it == blobs.end()) { return kResultFalse; }
        d = it->second.data();
        n = (uint32)it->second.size();
        return kResultOk;
    }
};

class Message : public IMessage {
public:
    std::string       id;
    Attributes        attrs;
    std::atomic<int>  rc{1};

    tresult PLUGIN_API queryInterface(const TUID iid, void ** o) override {
        QUERY_INTERFACE(iid, o, FUnknown::iid, IMessage)
        QUERY_INTERFACE(iid, o, IMessage::iid, IMessage)
        *o = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef(void) override { return (uint32)++rc; }
    uint32 PLUGIN_API release(void) override {
        int c = --rc;
        if (c == 0) { delete this; return 0; }
        return (uint32)c;
    }
    FIDString PLUGIN_API getMessageID(void) override { return id.c_str(); }
    void PLUGIN_API setMessageID(FIDString newId) override { id = (newId != nullptr) ? newId : ""; }
    IAttributeList * PLUGIN_API getAttributes(void) override { return &attrs; }
};

class HostApp : public IHostApplication {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void ** o) override {
        QUERY_INTERFACE(iid, o, FUnknown::iid, IHostApplication)
        QUERY_INTERFACE(iid, o, IHostApplication::iid, IHostApplication)
        *o = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef(void) override { return 1; }
    uint32 PLUGIN_API release(void) override { return 1; }

    tresult PLUGIN_API getName(String128 name) override {
        const char * n = "vst3hostwin";
        int          i = 0;
        for (; n[i] != 0; i++) { name[i] = (TChar)n[i]; }
        name[i] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID cid, TUID, void ** obj) override {
        if (memcmp(cid, IMessage::iid.toTUID(), sizeof(TUID)) == 0) {
            *obj = (IMessage *)new Message();
            return kResultOk;
        }
        *obj = nullptr;
        return kResultFalse;
    }
};

class MemStream : public IBStream {
public:
    std::string buf;
    size_t      pos = 0;
    NO_QUERY

    tresult PLUGIN_API read(void * b, int32 n, int32 * got) override {
        size_t avail = buf.size() - pos;
        size_t take  = ((size_t)n < avail) ? (size_t)n : avail;
        memcpy(b, buf.data() + pos, take);
        pos += take;
        if (got != nullptr) { *got = (int32)take; }
        return kResultOk;
    }
    tresult PLUGIN_API write(void * b, int32 n, int32 * put) override {
        buf.append((const char *)b, (size_t)n);
        if (put != nullptr) { *put = n; }
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 p, int32 mode, int64 * r) override {
        int64 base = (mode == kIBSeekCur) ? (int64)pos : ((mode == kIBSeekEnd) ? (int64)buf.size() : 0);
        pos = (size_t)std::max<int64>(0, std::min<int64>(base + p, (int64)buf.size()));
        if (r != nullptr) { *r = (int64)pos; }
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64 * p) override {
        if (p != nullptr) { *p = (int64)pos; }
        return kResultOk;
    }
};

// ------------------------------------------------------------------------------------------------
// What reaches the processor: notes and parameter changes, queued by any thread, taken per block
// ------------------------------------------------------------------------------------------------

static std::mutex                gInputLock;
static std::vector<Event>        gPendingEvents;
static std::map<ParamID, double> gPendingParams;

static void queue_note(int pitch, float velocity, bool on, int channel = 0) {
    Event e = {};

    e.busIndex     = 0;
    e.sampleOffset = 0;

    if (on) {
        e.type             = Event::kNoteOnEvent;
        e.noteOn.channel   = (int16)channel;
        e.noteOn.pitch     = (int16)pitch;
        e.noteOn.velocity  = velocity;
        e.noteOn.noteId    = -1;
    } else {
        e.type             = Event::kNoteOffEvent;
        e.noteOff.channel  = (int16)channel;
        e.noteOff.pitch    = (int16)pitch;
        e.noteOff.velocity = 0.0f;
        e.noteOff.noteId   = -1;
    }
    std::lock_guard<std::mutex> g(gInputLock);
    gPendingEvents.push_back(e);
}

static void queue_param(ParamID id, double normalized) {
    std::lock_guard<std::mutex> g(gInputLock);
    gPendingParams[id] = normalized;
}

class EventList : public IEventList {
public:
    std::vector<Event> events;
    NO_QUERY
    int32 PLUGIN_API getEventCount(void) override { return (int32)events.size(); }
    tresult PLUGIN_API getEvent(int32 i, Event & e) override {
        if ((i < 0) || (i >= (int32)events.size())) { return kResultFalse; }
        e = events[(size_t)i];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event & e) override { events.push_back(e); return kResultOk; }
};

class ParamQueue : public IParamValueQueue {
public:
    ParamID id    = 0;
    double  value = 0.0;
    NO_QUERY
    ParamID PLUGIN_API getParameterId(void) override { return id; }
    int32 PLUGIN_API getPointCount(void) override { return 1; }
    tresult PLUGIN_API getPoint(int32 i, int32 & offset, ParamValue & v) override {
        if (i != 0) { return kResultFalse; }
        offset = 0;
        v      = value;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32 &) override { return kResultFalse; }
};

class ParamChanges : public IParameterChanges {
public:
    std::vector<ParamQueue> queues;
    NO_QUERY
    int32 PLUGIN_API getParameterCount(void) override { return (int32)queues.size(); }
    IParamValueQueue * PLUGIN_API getParameterData(int32 i) override {
        return ((i >= 0) && (i < (int32)queues.size())) ? &queues[(size_t)i] : nullptr;
    }
    IParamValueQueue * PLUGIN_API addParameterData(const ParamID &, int32 &) override { return nullptr; }
};

// notes §3: the controller's edits (a dial turned in the editor) reach the processor as they do in a host
class ComponentHandler : public IComponentHandler {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void ** o) override {
        QUERY_INTERFACE(iid, o, FUnknown::iid, IComponentHandler)
        QUERY_INTERFACE(iid, o, IComponentHandler::iid, IComponentHandler)
        *o = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef(void) override { return 1; }
    uint32 PLUGIN_API release(void) override { return 1; }
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override { queue_param(id, v); return kResultOk; }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
};

// ------------------------------------------------------------------------------------------------
// The plug-in, and the window it is shown in
// ------------------------------------------------------------------------------------------------

static HostApp            gHostApp;
static ComponentHandler   gHandler;
static IComponent *       gComponent  = nullptr;
static IAudioProcessor *  gProcessor  = nullptr;
static IEditController *  gController = nullptr;
static IPlugView *        gView       = nullptr;
static IPlugViewContentScaleSupport * gScaleSupport = nullptr;
static HWND               gWindow     = nullptr;
static bool               gResizing   = false;    // our own SetWindowPos, not the user's drag
static int                gOctave     = 4;        // the computer keyboard's C is C4 = 60
static bool               gKeyDown[256] = {};
static ParamID            gMidiMap[16][130];
static bool               gMidiMapped[16][130] = {};

static void set_client_size(int width, int height) {
    RECT r     = {0, 0, width, height};
    DWORD style = (DWORD)GetWindowLongPtrW(gWindow, GWL_STYLE);

    typedef BOOL (WINAPI * tAdjustForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
    typedef UINT (WINAPI * tGetDpiForWindow)(HWND);
    HMODULE          user32 = GetModuleHandleW(L"user32.dll");
    tAdjustForDpi    adjust = (tAdjustForDpi)(void *)GetProcAddress(user32, "AdjustWindowRectExForDpi");
    tGetDpiForWindow dpiOf  = (tGetDpiForWindow)(void *)GetProcAddress(user32, "GetDpiForWindow");

    if ((adjust != nullptr) && (dpiOf != nullptr)) {
        adjust(&r, style, FALSE, 0, dpiOf(gWindow));
    } else {
        AdjustWindowRectEx(&r, style, FALSE, 0);
    }
    gResizing = true;
    SetWindowPos(gWindow, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    gResizing = false;
}

class HostFrame : public IPlugFrame {
public:
    tresult PLUGIN_API queryInterface(const TUID iid, void ** o) override {
        QUERY_INTERFACE(iid, o, FUnknown::iid, IPlugFrame)
        QUERY_INTERFACE(iid, o, IPlugFrame::iid, IPlugFrame)
        *o = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef(void) override { return 1; }
    uint32 PLUGIN_API release(void) override { return 1; }

    tresult PLUGIN_API resizeView(IPlugView * view, ViewRect * rect) override {
        if ((rect == nullptr) || (gWindow == nullptr)) {
            return kResultFalse;
        }
        set_client_size(rect->getWidth(), rect->getHeight());

        if (view != nullptr) {
            view->onSize(rect);
        }
        return kResultTrue;
    }
};

static HostFrame gFrame;

static double window_scale(HWND hwnd) {
    typedef UINT (WINAPI * tGetDpiForWindow)(HWND);
    tGetDpiForWindow dpiOf = (tGetDpiForWindow)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    UINT             dpi   = (dpiOf != nullptr) ? dpiOf(hwnd) : 96u;

    return (double)dpi / 96.0;
}

// notes §4: Caps Lock on, the letters play; off, they are the plug-in's to type with
static int note_for_key(WPARAM vk) {
    static const char * keys = "AWSEDFTGYHUJKOLP";    // C up to D#, a piano's white and black keys

    for (int i = 0; keys[i] != 0; i++) {
        if ((WPARAM)keys[i] == vk) {
            return (gOctave + 1) * 12 + i;
        }
    }
    return -1;
}

static LRESULT CALLBACK keyboard_hook(int code, WPARAM wParam, LPARAM lParam) {
    if ((code == HC_ACTION) && ((GetKeyState(VK_CAPITAL) & 1) != 0) && (wParam < 256)) {
        bool up      = ((lParam & (1u << 31)) != 0);
        bool wasDown = ((lParam & (1u << 30)) != 0);

        if ((wParam == 'Z') || (wParam == 'X')) {
            if (!up && !wasDown) {
                gOctave = std::max(0, std::min(8, gOctave + ((wParam == 'X') ? 1 : -1)));
                printf("octave: C%d\n", gOctave);
            }
            return 1;
        }
        int note = note_for_key(wParam);

        if (note >= 0) {
            if (!up && !gKeyDown[wParam]) {
                gKeyDown[wParam] = true;
                queue_note(note, 0.8f, true);
            } else if (up && gKeyDown[wParam]) {
                gKeyDown[wParam] = false;
                queue_note(note, 0.0f, false);
            }
            return 1;
        }
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

static LRESULT CALLBACK host_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_SIZE:
            if (!gResizing && (gView != nullptr) && (wParam != SIZE_MINIMIZED)) {
                ViewRect r = {0, 0, (int32)LOWORD(lParam), (int32)HIWORD(lParam)};

                if (gView->canResize() == kResultTrue) {
                    gView->checkSizeConstraint(&r);
                    gView->onSize(&r);

                    if ((r.getWidth() != (int32)LOWORD(lParam)) || (r.getHeight() != (int32)HIWORD(lParam))) {
                        set_client_size(r.getWidth(), r.getHeight());    // the aspect the plug-in keeps
                    }
                }
            }
            return 0;

        case WM_DPICHANGED: {    // notes §5
            RECT * wanted = (RECT *)lParam;

            gResizing = true;
            SetWindowPos(hwnd, nullptr, wanted->left, wanted->top, wanted->right - wanted->left,
                         wanted->bottom - wanted->top, SWP_NOZORDER | SWP_NOACTIVATE);
            gResizing = false;

            if (gScaleSupport != nullptr) {
                gScaleSupport->setContentScaleFactor((float)(HIWORD(wParam) / 96.0));
            }
            ViewRect r = {};

            if ((gView != nullptr) && (gView->getSize(&r) == kResultTrue)) {
                set_client_size(r.getWidth(), r.getHeight());
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ------------------------------------------------------------------------------------------------
// MIDI in (winmm) and audio out (miniaudio, WASAPI)
// ------------------------------------------------------------------------------------------------

static void CALLBACK midi_in(HMIDIIN, UINT msg, DWORD_PTR, DWORD_PTR data, DWORD_PTR) {
    if (msg != MIM_DATA) {
        return;
    }
    int status  = (int)(data & 0xF0);
    int channel = (int)(data & 0x0F);
    int d1      = (int)((data >> 8) & 0x7F);
    int d2      = (int)((data >> 16) & 0x7F);
    int ctrl    = -1;
    double v    = 0.0;

    switch (status) {
        case 0x90: queue_note(d1, (float)d2 / 127.0f, d2 > 0, channel); return;
        case 0x80: queue_note(d1, 0.0f, false, channel); return;
        case 0xB0: ctrl = d1; v = (double)d2 / 127.0; break;
        case 0xD0: ctrl = kAfterTouch; v = (double)d1 / 127.0; break;
        case 0xE0: ctrl = kPitchBend; v = (double)((d2 << 7) | d1) / 16383.0; break;
        default: return;
    }

    if ((ctrl >= 0) && (ctrl < 130) && gMidiMapped[channel][ctrl]) {
        queue_param(gMidiMap[channel][ctrl], v);    // notes §6
    }
}

static float          gLeft[MAX_BLOCK];
static float          gRight[MAX_BLOCK];
static EventList      gBlockEvents;
static ParamChanges   gBlockParams;

static void audio_callback(ma_device * device, void * out, const void *, ma_uint32 frames) {
    float * o = (float *)out;
    int     ch = (int)device->playback.channels;

    while (frames > 0) {
        ma_uint32 n = (frames > MAX_BLOCK) ? MAX_BLOCK : frames;

        gBlockEvents.events.clear();
        gBlockParams.queues.clear();
        {
            std::unique_lock<std::mutex> g(gInputLock, std::try_to_lock);    // never wait on the audio thread

            if (g.owns_lock()) {
                gBlockEvents.events.swap(gPendingEvents);

                for (auto & p : gPendingParams) {
                    ParamQueue q;
                    q.id    = p.first;
                    q.value = p.second;
                    gBlockParams.queues.push_back(q);
                }
                gPendingParams.clear();
            }
        }
        float *         chans[2] = {gLeft, gRight};
        AudioBusBuffers bus      = {};
        ProcessData     data     = {};

        bus.numChannels         = 2;
        bus.channelBuffers32    = chans;
        data.processMode        = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples         = (int32)n;
        data.numOutputs         = 1;
        data.outputs            = &bus;
        data.inputEvents        = &gBlockEvents;
        data.inputParameterChanges = &gBlockParams;
        memset(gLeft, 0, sizeof(gLeft));
        memset(gRight, 0, sizeof(gRight));
        gProcessor->process(data);

        for (ma_uint32 i = 0; i < n; i++) {
            o[0] = gLeft[i];

            if (ch > 1) {
                o[1] = gRight[i];
            }
            o += ch;
        }
        frames -= n;
    }
}

// ------------------------------------------------------------------------------------------------

static void usage(void) {
    fprintf(stderr,
            "usage: vst3hostwin <plugin.vst3> [--midi N] [--seconds N] [--shot out.bmp] [--dpi-unaware]\n"
            "                   [--no-audio] [--audio N] [--note PITCH] [--fresh] [--keep-state]\n"
            "  <plugin.vst3>  a single-file .vst3, or a bundle folder (Contents\\<arch>-win\\<name>.vst3)\n"
            "  --midi N       open MIDI input N (the list is printed at start-up)\n"
            "  --seconds N    quit after N seconds\n"
            "  --shot PATH    save the window as a .bmp just before quitting; implies --seconds 4\n"
            "  --dpi-unaware  run as a host Windows scales as a bitmap, for that half of the DPI handling\n"
            "  --no-audio     the editor only\n"
            "  --audio N      which audio class to load when the factory registers several (default 0)\n"
            "  --note PITCH   hold this note from the start\n"
            "  --fresh        do not restore the saved state (%%APPDATA%%\\vst3hostwin\\<plugin>.state)\n"
            "  --keep-state   do not save the state at exit\n"
            "Caps Lock on: A W S E D F T G Y H U J K O L P play C to D#, Z and X shift the octave.\n");
}

// notes §8: GenBridge Monitor's habit - the plug-in's state kept per plug-in, restored at open, saved at close
static std::wstring state_path(const std::wstring & binary) {
    wchar_t      appData[MAX_PATH] = {0};
    DWORD        n                 = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    std::wstring dir               = ((n > 0) && (n < MAX_PATH)) ? std::wstring(appData) + L"\\vst3hostwin" : L".";
    size_t       slash             = binary.find_last_of(L"\\/");
    std::wstring leaf              = (slash == std::wstring::npos) ? binary : binary.substr(slash + 1);

    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\" + leaf + L".state";
}

static bool read_file(const std::wstring & path, std::string & out) {
    FILE * f = _wfopen(path.c_str(), L"rb");

    if (f == nullptr) {
        return false;
    }
    char   chunk[65536];
    size_t got;

    out.clear();

    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        out.append(chunk, got);
    }
    fclose(f);
    return !out.empty();
}

static void write_file(const std::wstring & path, const std::string & data) {
    FILE * f = _wfopen(path.c_str(), L"wb");

    if (f != nullptr) {
        fwrite(data.data(), 1, data.size(), f);
        fclose(f);
    }
}

static std::wstring widen(const char * s) {
    int          n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w((size_t)((n > 0) ? n - 1 : 0), L'\0');

    MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
    return w;
}

// notes §7
static std::wstring binary_path(const std::wstring & path) {
    DWORD attrs = GetFileAttributesW(path.c_str());

    if ((attrs == INVALID_FILE_ATTRIBUTES) || ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)) {
        return path;
    }
    std::wstring name = path;

    while (!name.empty() && ((name.back() == L'\\') || (name.back() == L'/'))) {
        name.pop_back();
    }
    size_t       slash = name.find_last_of(L"\\/");
    std::wstring leaf  = (slash == std::wstring::npos) ? name : name.substr(slash + 1);
#if defined (__aarch64__)
    const wchar_t * arch = L"arm64-win";
#else
    const wchar_t * arch = L"x86_64-win";
#endif
    return name + L"\\Contents\\" + arch + L"\\" + leaf;
}

static void save_shot(const char * path) {
    RECT r;

    GetClientRect(gWindow, &r);
    POINT origin = {0, 0};

    ClientToScreen(gWindow, &origin);
    int     w      = r.right;
    int     h      = r.bottom;
    HDC     screen = GetDC(nullptr);
    HDC     mem    = CreateCompatibleDC(screen);
    HBITMAP bmp    = CreateCompatibleBitmap(screen, w, h);

    SelectObject(mem, bmp);

    // PW_RENDERFULLCONTENT: the window as the compositor has it, OpenGL child included, even when covered
    if (!PrintWindow(gWindow, mem, PW_CLIENTONLY | 0x2u)) {
        BitBlt(mem, 0, 0, w, h, screen, origin.x, origin.y, SRCCOPY);
    }

    BITMAPINFOHEADER bi = {};

    bi.biSize        = sizeof(bi);
    bi.biWidth       = w;
    bi.biHeight      = h;
    bi.biPlanes      = 1;
    bi.biBitCount    = 24;
    bi.biCompression = BI_RGB;
    int                stride = ((w * 3) + 3) & ~3;
    std::vector<char>  pixels((size_t)stride * (size_t)h);

    GetDIBits(mem, bmp, 0, (UINT)h, pixels.data(), (BITMAPINFO *)&bi, DIB_RGB_COLORS);
    BITMAPFILEHEADER bf = {};

    bf.bfType    = 0x4D42;
    bf.bfOffBits = sizeof(bf) + sizeof(bi);
    bf.bfSize    = bf.bfOffBits + (DWORD)pixels.size();
    FILE * f     = fopen(path, "wb");

    if (f != nullptr) {
        fwrite(&bf, sizeof(bf), 1, f);
        fwrite(&bi, sizeof(bi), 1, f);
        fwrite(pixels.data(), 1, pixels.size(), f);
        fclose(f);
        printf("shot: %s (%dx%d)\n", path, w, h);
    }
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

int main(int argc, char ** argv) {
    if (argc < 2) {
        usage();
        return 2;
    }
    const char * pluginPath = argv[1];
    const char * shotPath   = nullptr;
    double       seconds    = 0.0;
    int          midiIndex  = -1;
    int          audioIndex = 0;
    int          heldNote   = -1;
    bool         dpiAware   = true;
    bool         audio      = true;
    bool         freshState    = false;
    bool         readOnlyState = false;

    for (int i = 2; i < argc; i++) {
        if ((strcmp(argv[i], "--seconds") == 0) && (i + 1 < argc)) {
            seconds = atof(argv[++i]);
        } else if ((strcmp(argv[i], "--shot") == 0) && (i + 1 < argc)) {
            shotPath = argv[++i];
            if (seconds <= 0.0) { seconds = 4.0; }
        } else if ((strcmp(argv[i], "--midi") == 0) && (i + 1 < argc)) {
            midiIndex = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--audio") == 0) && (i + 1 < argc)) {
            audioIndex = atoi(argv[++i]);
        } else if ((strcmp(argv[i], "--note") == 0) && (i + 1 < argc)) {
            heldNote = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--dpi-unaware") == 0) {
            dpiAware = false;
        } else if (strcmp(argv[i], "--no-audio") == 0) {
            audio = false;
        } else if (strcmp(argv[i], "--fresh") == 0) {
            freshState = true;
        } else if (strcmp(argv[i], "--keep-state") == 0) {
            readOnlyState = true;
        } else {
            usage();
            return 2;
        }
    }

    // notes §5: as the DAWs that scale their plug-ins are; --dpi-unaware for the others
    if (dpiAware) {
        typedef BOOL (WINAPI * tSetContext)(HANDLE);
        tSetContext set = (tSetContext)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext");

        if ((set == nullptr) || !set((HANDLE)-4)) {    // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
            SetProcessDPIAware();
        }
    }
    printf("host: %s\n", dpiAware ? "per-monitor DPI aware" : "DPI unaware (Windows scales it as a bitmap)");

    // --- the DLL, its factory, the component and controller --------------------------------------
    std::wstring binary = binary_path(widen(pluginPath));
    HMODULE      dll    = LoadLibraryW(binary.c_str());

    if (dll == nullptr) {
        fprintf(stderr, "LoadLibrary failed (error %lu): %ls\n", GetLastError(), binary.c_str());
        return 1;
    }
    typedef bool (*tInitDll)(void);
    typedef bool (*tExitDll)(void);
    typedef IPluginFactory * (*tGetFactory)(void);
    tInitDll    initDll    = (tInitDll)(void *)GetProcAddress(dll, "InitDll");
    tExitDll    exitDll    = (tExitDll)(void *)GetProcAddress(dll, "ExitDll");
    tGetFactory getFactory = (tGetFactory)(void *)GetProcAddress(dll, "GetPluginFactory");

    if (getFactory == nullptr) {
        fprintf(stderr, "no GetPluginFactory export\n");
        return 1;
    }

    if (initDll != nullptr) {
        initDll();
    }
    IPluginFactory * factory = getFactory();

    if (factory == nullptr) {
        fprintf(stderr, "GetPluginFactory returned null\n");
        return 1;
    }
    IPluginFactory2 * factory2 = nullptr;

    if (factory->queryInterface(IPluginFactory2::iid, (void **)&factory2) != kResultTrue) {
        factory2 = nullptr;
    }
    printf("factory: %d classes, IPluginFactory2 %s\n", factory->countClasses(),
           (factory2 != nullptr) ? "yes" : "NO - a host may refuse this");

    TUID componentCid;
    TUID controllerCid;
    bool haveComponent  = false;
    bool haveController = false;
    int  audioSeen      = 0;

    for (int32 i = 0; i < factory->countClasses(); i++) {
        PClassInfo info = {};

        if (factory->getClassInfo(i, &info) != kResultTrue) {
            continue;
        }
        PClassInfo2  info2 = {};
        const char * sub   = "";

        if ((factory2 != nullptr) && (factory2->getClassInfo2(i, &info2) == kResultTrue)) {
            sub = info2.subCategories;
        }
        printf("  [%d] %-28s %-24s %s\n", i, info.name, info.category, sub);

        if ((strcmp(info.category, kVstAudioEffectClass) == 0) && (audioSeen++ == audioIndex)) {
            memcpy(componentCid, info.cid, sizeof(TUID));
            haveComponent = true;
        } else if (strcmp(info.category, kVstComponentControllerClass) == 0) {
            memcpy(controllerCid, info.cid, sizeof(TUID));
            haveController = true;
        }
    }

    if (!haveComponent
       || (factory->createInstance(componentCid, IComponent::iid, (void **)&gComponent) != kResultTrue)
       || (gComponent == nullptr)) {
        fprintf(stderr, "no audio class %d, or it could not be created\n", audioIndex);
        return 1;
    }
    gComponent->initialize(&gHostApp);

    // The controller the component names - not whichever the factory listed last
    if (gComponent->getControllerClassId(controllerCid) == kResultTrue) {
        haveController = true;
    }

    if (!haveController
       || (factory->createInstance(controllerCid, IEditController::iid, (void **)&gController) != kResultTrue)
       || (gController == nullptr)) {
        fprintf(stderr, "no controller, or it could not be created\n");
        return 1;
    }
    gController->initialize(&gHostApp);
    gController->setComponentHandler(&gHandler);

    // Connected, as a host connects them: how the controller learns which processor is its own
    IConnectionPoint * compPoint = nullptr;
    IConnectionPoint * ctrlPoint = nullptr;

    gComponent->queryInterface(IConnectionPoint::iid, (void **)&compPoint);
    gController->queryInterface(IConnectionPoint::iid, (void **)&ctrlPoint);

    if ((compPoint != nullptr) && (ctrlPoint != nullptr)) {
        compPoint->connect(ctrlPoint);
        ctrlPoint->connect(compPoint);
    }
    std::wstring statePath = state_path(binary);
    {
        MemStream saved;

        if (!freshState && read_file(statePath, saved.buf)) {
            gComponent->setState(&saved);
            printf("state: restored %zu bytes from %ls\n", saved.buf.size(), statePath.c_str());
        }
        MemStream state;

        if (gComponent->getState(&state) == kResultTrue) {
            state.pos = 0;
            gController->setComponentState(&state);
        }
    }
    printf("controller: %d parameters\n", gController->getParameterCount());

    IMidiMapping * mapping = nullptr;

    if ((gController->queryInterface(IMidiMapping::iid, (void **)&mapping) == kResultTrue) && (mapping != nullptr)) {
        for (int ch = 0; ch < 16; ch++) {
            for (int c = 0; c < 130; c++) {
                gMidiMapped[ch][c] = (mapping->getMidiControllerAssignment(0, (int16)ch, (CtrlNumber)c, gMidiMap[ch][c]) == kResultTrue);
            }
        }
        mapping->release();
    }

    // --- audio -------------------------------------------------------------------------------------
    ma_device device;
    bool      deviceRunning = false;

    if (gComponent->queryInterface(IAudioProcessor::iid, (void **)&gProcessor) != kResultTrue) {
        gProcessor = nullptr;
    }

    if (audio && (gProcessor != nullptr)) {
        ma_device_config config = ma_device_config_init(ma_device_type_playback);

        config.playback.format   = ma_format_f32;
        config.playback.channels = 2;
        config.dataCallback      = audio_callback;

        if (ma_device_init(nullptr, &config, &device) == MA_SUCCESS) {
            ProcessSetup setup = {kRealtime, kSample32, MAX_BLOCK, (SampleRate)device.sampleRate};

            gComponent->activateBus(kAudio, kOutput, 0, true);
            gComponent->activateBus(kEvent, kInput, 0, true);
            gProcessor->setupProcessing(setup);
            gComponent->setActive(true);
            gProcessor->setProcessing(true);

            if (heldNote >= 0) {
                queue_note(heldNote, 0.8f, true);
            }

            if (ma_device_start(&device) == MA_SUCCESS) {
                deviceRunning = true;
                printf("audio: %s, %u Hz\n", device.playback.name, device.sampleRate);
            }
        } else {
            fprintf(stderr, "audio: no output device\n");
        }
    }

    // --- MIDI ----------------------------------------------------------------------------------------
    HMIDIIN midi = nullptr;
    UINT    ins  = midiInGetNumDevs();

    for (UINT i = 0; i < ins; i++) {
        MIDIINCAPSW caps = {};

        midiInGetDevCapsW(i, &caps, sizeof(caps));
        printf("midi in %u: %ls%s\n", i, caps.szPname, ((int)i == midiIndex) ? "  <- open" : "");
    }

    if ((midiIndex >= 0) && (midiInOpen(&midi, (UINT)midiIndex, (DWORD_PTR)midi_in, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)) {
        midiInStart(midi);
    } else if (midiIndex >= 0) {
        fprintf(stderr, "midi: could not open input %d\n", midiIndex);
    }

    // --- the editor ----------------------------------------------------------------------------------
    WNDCLASSEXW wc = {};

    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = host_proc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"vst3hostwin";
    RegisterClassExW(&wc);

    std::wstring title = L"vst3hostwin - " + widen(pluginPath);

    gWindow = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
    gView   = gController->createView(ViewType::kEditor);

    if (gView == nullptr) {
        fprintf(stderr, "createView returned null - no editor\n");
    } else if (gView->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
        fprintf(stderr, "the view does not support HWND\n");
        gView->release();
        gView = nullptr;
    }

    if (gView != nullptr) {
        // notes §5: the scale BEFORE the size is asked, as Steinberg's own editor host does
        if (gView->queryInterface(IPlugViewContentScaleSupport::iid, (void **)&gScaleSupport) != kResultTrue) {
            gScaleSupport = nullptr;
        }
        double scale = window_scale(gWindow);

        if (gScaleSupport != nullptr) {
            gScaleSupport->setContentScaleFactor((float)scale);
        }
        printf("view: content scale %.2f %s\n", scale,
               (gScaleSupport != nullptr) ? "(IPlugViewContentScaleSupport)" : "(NOT supported by the plug-in)");

        ViewRect size = {};

        if (gView->getSize(&size) != kResultTrue) {
            size = {0, 0, 1000, 700};
        }
        printf("view: %dx%d pixels\n", size.getWidth(), size.getHeight());
        set_client_size(size.getWidth(), size.getHeight());
        gView->setFrame(&gFrame);

        if (gView->attached((void *)gWindow, kPlatformTypeHWND) != kResultOk) {
            fprintf(stderr, "attached() refused\n");
        }
    }
    ShowWindow(gWindow, SW_SHOW);
    UpdateWindow(gWindow);

    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD, keyboard_hook, nullptr, GetCurrentThreadId());

    if (seconds > 0.0) {
        SetTimer(gWindow, 1, (UINT)(seconds * 1000.0), nullptr);
    }
    MSG m;

    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if ((m.message == WM_TIMER) && (m.hwnd == gWindow) && (m.wParam == 1)) {
            if (shotPath != nullptr) {
                save_shot(shotPath);
            }
            DestroyWindow(gWindow);
            continue;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    // --- down in the reverse order -------------------------------------------------------------------
    if (hook != nullptr) {
        UnhookWindowsHookEx(hook);
    }

    if (midi != nullptr) {
        midiInStop(midi);
        midiInClose(midi);
    }

    if (deviceRunning) {
        ma_device_uninit(&device);
        gProcessor->setProcessing(false);
        gComponent->setActive(false);
    }

    if (gView != nullptr) {    // the editor first: it must not outlive the plug-in (as GenBridge Monitor)
        gView->removed();
        if (gScaleSupport != nullptr) { gScaleSupport->release(); }
        gView->release();
    }
    {
        MemStream state;

        if (!readOnlyState && (gComponent->getState(&state) == kResultTrue) && !state.buf.empty()) {
            write_file(statePath, state.buf);
            printf("state: saved %zu bytes to %ls\n", state.buf.size(), statePath.c_str());
        }
    }

    if ((compPoint != nullptr) && (ctrlPoint != nullptr)) {
        compPoint->disconnect(ctrlPoint);
        ctrlPoint->disconnect(compPoint);
    }
    if (compPoint != nullptr) { compPoint->release(); }
    if (ctrlPoint != nullptr) { ctrlPoint->release(); }
    if (gProcessor != nullptr) { gProcessor->release(); }
    gController->terminate();
    gController->release();
    gComponent->terminate();
    gComponent->release();

    if (factory2 != nullptr) { factory2->release(); }
    factory->release();

    if (exitDll != nullptr) {
        exitDll();
    }
    return 0;
}
