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
#include <commdlg.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
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
static int                gKeyNote[256] = {};    // the note each held key started, for its release
static bool               gKeysPlay     = true;  // the Keyboard menu's "Letters play notes"
static ParamID            gMidiMap[16][130];
static bool               gMidiMapped[16][130] = {};
static bool               gHaveConsole = true;    // false when started from an icon (notes §9)
static std::wstring       gPluginName;
static std::wstring       gRelaunch;              // the plug-in chosen from the menu, opened in a fresh process

static void         update_title(void);
static std::wstring choose_plugin(HWND owner);
static void         midi_menu_chosen(int item);
static void         keys_menu_chosen(void);
static void         show_help(HWND owner);

enum { kMenuOpen = 1001, kMenuExit, kMenuHelp, kMenuKeys, kMenuOctaveDown, kMenuOctaveUp,
       kMenuMidiNone = 1100, kMenuMidiFirst = 1101 };
static const UINT kMsgTitle = WM_APP + 1;    // Caps Lock changed: the title says what the letters do

static void set_client_size_of(HWND hwnd, int width, int height, bool move) {
    RECT r     = {0, 0, width, height};
    DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    BOOL  menu  = (GetMenu(hwnd) != nullptr) ? TRUE : FALSE;

    typedef BOOL (WINAPI * tAdjustForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
    typedef UINT (WINAPI * tGetDpiForWindow)(HWND);
    HMODULE          user32 = GetModuleHandleW(L"user32.dll");
    tAdjustForDpi    adjust = (tAdjustForDpi)(void *)GetProcAddress(user32, "AdjustWindowRectExForDpi");
    tGetDpiForWindow dpiOf  = (tGetDpiForWindow)(void *)GetProcAddress(user32, "GetDpiForWindow");

    if ((adjust != nullptr) && (dpiOf != nullptr)) {
        adjust(&r, style, menu, 0, dpiOf(hwnd));
    } else {
        AdjustWindowRectEx(&r, style, menu, 0);
    }
    SetWindowPos(hwnd, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                 (move ? 0u : SWP_NOMOVE) | SWP_NOZORDER | SWP_NOACTIVATE);
}

static void set_client_size(int width, int height) {
    gResizing = true;
    set_client_size_of(gWindow, width, height, false);
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

// notes §4: G2Alike's layout - A S D F G H J K L the white keys from C, W E T Y U O P the black
static int note_for_key(WPARAM vk) {
    static const char * keys = "AWSEDFTGYHUJKOLP";    // C up to D#, a piano's white and black keys

    for (int i = 0; keys[i] != 0; i++) {
        if ((WPARAM)keys[i] == vk) {
            return (gOctave + 1) * 12 + i;
        }
    }
    return -1;
}

static bool caps_lock_on(void) {
    return (GetKeyState(VK_CAPITAL) & 1) != 0;
}

// The letters play only in the plug-in's own window, never in the browser, a file dialog or a message box
static bool keys_play_now(void) {
    return gKeysPlay && !caps_lock_on() && (gWindow != nullptr) && (GetActiveWindow() == gWindow);
}

static bool modifier_down(void) {
    return (GetKeyState(VK_CONTROL) < 0) || (GetKeyState(VK_MENU) < 0) || (GetKeyState(VK_LWIN) < 0) || (GetKeyState(VK_RWIN) < 0);
}

static void shift_octave(int by) {
    gOctave = std::max(0, std::min(8, gOctave + by));
    printf("octave: C%d\n", gOctave);
    update_title();
}

// Every key still down, let go - the window lost the keyboard, so their releases will never come here
static void release_keys(void) {
    for (int k = 0; k < 256; k++) {
        if (gKeyDown[k]) {
            gKeyDown[k] = false;
            queue_note(gKeyNote[k], 0.0f, false);
        }
    }
}

static LRESULT CALLBACK keyboard_hook(int code, WPARAM wParam, LPARAM lParam) {
    if ((code == HC_ACTION) && (wParam < 256)) {
        bool up      = ((lParam & (1u << 31)) != 0);
        bool wasDown = ((lParam & (1u << 30)) != 0);

        if (wParam == VK_CAPITAL) {
            if (up && (gWindow != nullptr)) {
                PostMessageW(gWindow, kMsgTitle, 0, 0);
            }
            return CallNextHookEx(nullptr, code, wParam, lParam);
        }

        // A release always ends the note its press started, whatever the octave or Caps Lock is now
        if (up && gKeyDown[wParam]) {
            gKeyDown[wParam] = false;
            queue_note(gKeyNote[wParam], 0.0f, false);
            return 1;
        }

        if (keys_play_now() && !modifier_down()) {
            if ((wParam == 'Z') || (wParam == 'X')) {
                if (!up && !wasDown) {
                    shift_octave((wParam == 'X') ? 1 : -1);
                }
                return 1;
            }
            int note = note_for_key(wParam);

            if (note >= 0) {
                if (!up && !gKeyDown[wParam]) {
                    gKeyDown[wParam] = true;
                    gKeyNote[wParam] = note;
                    queue_note(note, 0.8f, true);
                    printf("key %c: note %d\n", (char)wParam, note);
                }
                return 1;
            }
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

        case WM_COMMAND:    // the menu bar's own items; the plug-in's controls report to its own window
            if ((lParam == 0) && (HIWORD(wParam) == 0)) {
                int id = (int)LOWORD(wParam);

                if (id == kMenuOpen) {
                    std::wstring path = choose_plugin(hwnd);

                    if (!path.empty()) {
                        gRelaunch = path;    // notes §11
                        DestroyWindow(hwnd);
                    }
                } else if (id == kMenuExit) {
                    DestroyWindow(hwnd);
                } else if (id == kMenuHelp) {
                    show_help(hwnd);
                } else if (id == kMenuKeys) {
                    keys_menu_chosen();
                } else if ((id == kMenuOctaveDown) || (id == kMenuOctaveUp)) {
                    shift_octave((id == kMenuOctaveUp) ? 1 : -1);
                } else if (id >= kMenuMidiNone) {
                    midi_menu_chosen(id - kMenuMidiFirst);    // -1 for None
                }
                return 0;
            }
            break;

        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                release_keys();
            }
            break;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        default:
            if (msg == kMsgTitle) {
                update_title();
                return 0;
            }
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
            "usage: vst3hostwin [<plugin.vst3>] [--midi N] [--seconds N] [--shot out.bmp] [--dpi-unaware]\n"
            "                   [--no-audio] [--audio N] [--note PITCH] [--fresh] [--keep-state]\n"
            "  <plugin.vst3>  a single-file .vst3, or a bundle folder (Contents\\<arch>-win\\<name>.vst3);\n"
            "                 without one, a plug-in browser opens\n"
            "  --midi N       open MIDI input N (the list is printed at start-up; else the MIDI input menu's choice)\n"
            "  --seconds N    quit after N seconds\n"
            "  --shot PATH    save the window as a .bmp just before quitting; implies --seconds 4\n"
            "  --dpi-unaware  run as a host Windows scales as a bitmap, for that half of the DPI handling\n"
            "  --no-audio     the editor only\n"
            "  --audio N      which audio class to load when the factory registers several (default 0)\n"
            "  --note PITCH   hold this note from the start\n"
            "  --fresh        do not restore the saved state (%%APPDATA%%\\vst3hostwin\\<plugin>.state)\n"
            "  --keep-state   do not save the state at exit\n"
            "Keys: A S D F G H J K L white notes from C, W E T Y U O P black, Z / X octave;\n"
            "Caps Lock on types into the plug-in instead.\n");
}

// notes §8: GenBridge Monitor's habit - the plug-in's state kept per plug-in, restored at open, saved at close
static std::wstring settings_dir(void) {
    wchar_t      appData[MAX_PATH] = {0};
    DWORD        n                 = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    std::wstring dir               = ((n > 0) && (n < MAX_PATH)) ? std::wstring(appData) + L"\\vst3hostwin" : L".";

    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

static std::wstring leaf_of(const std::wstring & path) {
    std::wstring name = path;

    while (!name.empty() && ((name.back() == L'\\') || (name.back() == L'/'))) {
        name.pop_back();
    }
    size_t slash = name.find_last_of(L"\\/");

    return (slash == std::wstring::npos) ? name : name.substr(slash + 1);
}

static std::wstring state_path(const std::wstring & binary) {
    return settings_dir() + L"\\" + leaf_of(binary) + L".state";
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

static std::string narrow(const std::wstring & w) {
    int         n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s((size_t)((n > 0) ? n - 1 : 0), '\0');

    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

// notes §9: an error has to be seen, and from an icon there is no console to print it to
static void fail(const char * fmt, ...) {
    char    msg[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", msg);

    if (!gHaveConsole) {
        MessageBoxW(nullptr, widen(msg).c_str(), L"vst3hostwin", MB_OK | MB_ICONERROR);
    }
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

// ------------------------------------------------------------------------------------------------
// The plug-in browser (notes §10)
// ------------------------------------------------------------------------------------------------

#if defined (__aarch64__)
static const int       kOwnMachine = 0xAA64;
static const wchar_t * kOwnArch    = L"arm64";
#else
static const int       kOwnMachine = 0x8664;
static const wchar_t * kOwnArch    = L"x64";
#endif

// The PE header's machine field: 0xAA64 arm64 (ARM64X too), 0x8664 x64; -1 when it is not a DLL at all
static int pe_machine(const std::wstring & file) {
    FILE * f = _wfopen(file.c_str(), L"rb");
    int    m = -1;

    if (f == nullptr) {
        return -1;
    }
    unsigned char h[64];

    if ((fread(h, 1, sizeof(h), f) == sizeof(h)) && (h[0] == 'M') && (h[1] == 'Z')) {
        long          off = (long)((unsigned)h[0x3C] | ((unsigned)h[0x3D] << 8) | ((unsigned)h[0x3E] << 16) | ((unsigned)h[0x3F] << 24));
        unsigned char pe[6];

        if ((fseek(f, off, SEEK_SET) == 0) && (fread(pe, 1, sizeof(pe), f) == sizeof(pe)) && (memcmp(pe, "PE\0\0", 4) == 0)) {
            m = pe[4] | (pe[5] << 8);
        }
    }
    fclose(f);
    return m;
}

static const wchar_t * machine_name(int m) {
    return (m == 0xAA64) ? L"arm64" : ((m == 0x8664) ? L"x64" : ((m == 0x14C) ? L"x86" : L"unknown"));
}

struct PluginEntry {
    std::wstring name;
    std::wstring path;
    std::wstring where;
    std::wstring date;
    std::wstring problem;    // empty when this host can load it
};

static bool is_vst3_name(const std::wstring & n) {
    return (n.size() > 5) && (_wcsicmp(n.c_str() + n.size() - 5, L".vst3") == 0);
}

static void add_entry(std::vector<PluginEntry> & list, const std::wstring & path, const wchar_t * where) {
    for (const PluginEntry & e : list) {
        if (_wcsicmp(e.path.c_str(), path.c_str()) == 0) {
            return;
        }
    }
    PluginEntry  e;
    std::wstring binary = binary_path(path);
    std::wstring leaf   = leaf_of(path);
    int          m      = pe_machine(binary);

    e.name  = is_vst3_name(leaf) ? leaf.substr(0, leaf.size() - 5) : leaf;
    e.path  = path;
    e.where = where;

    WIN32_FILE_ATTRIBUTE_DATA fa = {};

    if (GetFileAttributesExW(binary.c_str(), GetFileExInfoStandard, &fa)) {
        FILETIME   local;
        SYSTEMTIME st;
        wchar_t    d[32];

        FileTimeToLocalFileTime(&fa.ftLastWriteTime, &local);
        FileTimeToSystemTime(&local, &st);
        swprintf(d, 32, L"%04u-%02u-%02u %02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
        e.date = d;
    }

    if (m < 0) {
        e.problem = std::wstring(L"no ") + kOwnArch + L" build in it";
    } else if (m != kOwnMachine) {
        e.problem = std::wstring(machine_name(m)) + L" only - this host is " + kOwnArch;
    }
    list.push_back(e);
}

static void scan_folder(std::vector<PluginEntry> & list, const std::wstring & dir, const wchar_t * where, int depth) {
    WIN32_FIND_DATAW fd;
    HANDLE           h = FindFirstFileW((dir + L"\\*").c_str(), &fd);

    if (h == INVALID_HANDLE_VALUE) {
        return;
    }

    do {
        std::wstring n = fd.cFileName;

        if ((n == L".") || (n == L"..")) {
            continue;
        }
        std::wstring full = dir + L"\\" + n;

        if (is_vst3_name(n)) {
            add_entry(list, full, where);    // a file or a bundle folder - never looked inside
        } else if (((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) && (depth > 0)) {
            scan_folder(list, full, where, depth - 1);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static std::vector<std::wstring> read_lines(const std::wstring & path) {
    std::vector<std::wstring> out;
    std::string               s;

    if (!read_file(path, s)) {
        return out;
    }
    size_t a = 0;

    while (a < s.size()) {
        size_t b = s.find('\n', a);

        b = (b == std::string::npos) ? s.size() : b;
        std::string line = s.substr(a, b - a);

        if (!line.empty() && (line.back() == '\r')) {
            line.pop_back();
        }
        if (!line.empty()) {
            out.push_back(widen(line.c_str()));
        }
        a = b + 1;
    }
    return out;
}

static void remember_recent(const std::wstring & path) {
    std::wstring              file = settings_dir() + L"\\recent.txt";
    std::vector<std::wstring> old  = read_lines(file);
    std::string               s    = narrow(path) + "\n";
    int                       kept = 0;

    for (const std::wstring & p : old) {
        if ((_wcsicmp(p.c_str(), path.c_str()) != 0) && (kept++ < 9)) {
            s += narrow(p) + "\n";
        }
    }
    write_file(file, s);
}

static std::wstring env_dir(const wchar_t * var, const wchar_t * tail) {
    wchar_t v[MAX_PATH] = {0};
    DWORD   n           = GetEnvironmentVariableW(var, v, MAX_PATH);

    return ((n > 0) && (n < MAX_PATH)) ? std::wstring(v) + tail : std::wstring();
}

static std::vector<PluginEntry> collect_plugins(void) {
    std::vector<PluginEntry> list;

    for (const std::wstring & p : read_lines(settings_dir() + L"\\recent.txt")) {
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) {
            add_entry(list, p, L"recent");
        }
    }

    // This checkout's own builds: the exe is SynthLib\tools\, the projects beside SynthLib
    wchar_t exe[MAX_PATH] = {0};

    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring root = exe;

    for (int up = 0; up < 3; up++) {
        size_t slash = root.find_last_of(L"\\/");
        root = (slash == std::wstring::npos) ? std::wstring() : root.substr(0, slash);
    }

    if (!root.empty() && (GetFileAttributesW((root + L"\\SynthLib").c_str()) != INVALID_FILE_ATTRIBUTES)) {
        WIN32_FIND_DATAW fd;
        HANDLE           h = FindFirstFileW((root + L"\\AllAlike\\*").c_str(), &fd);

        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) && (fd.cFileName[0] != L'.')) {
                    scan_folder(list, root + L"\\AllAlike\\" + fd.cFileName + L"\\build\\windows", L"AllAlike build", 0);
                }
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        h = FindFirstFileW((root + L"\\G2Alike\\build-win-*").c_str(), &fd);

        if (h != INVALID_HANDLE_VALUE) {
            do {
                scan_folder(list, root + L"\\G2Alike\\" + fd.cFileName, L"G2Alike build", 0);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    std::wstring common = env_dir(L"CommonProgramFiles", L"\\VST3");
    std::wstring user   = env_dir(L"LOCALAPPDATA", L"\\Programs\\Common\\VST3");

    if (!common.empty()) { scan_folder(list, common, L"installed", 3); }
    if (!user.empty()) { scan_folder(list, user, L"installed (user)", 3); }
    return list;
}

enum { kIdHeader = 201, kIdList, kIdPath, kIdBrowse };

struct Browser {
    HWND                     wnd    = nullptr;
    HWND                     header = nullptr;
    HWND                     list   = nullptr;
    HWND                     path   = nullptr;
    HWND                     open   = nullptr;
    HWND                     browse = nullptr;
    HWND                     cancel = nullptr;
    HFONT                    font   = nullptr;
    double                   scale  = 1.0;
    std::vector<PluginEntry> entries;
    std::wstring             chosen;
    bool                     done   = false;
};

static Browser * gBrowser = nullptr;

static void browser_layout(Browser * b) {
    RECT r;

    GetClientRect(b->wnd, &r);
    auto S      = [b](int v) { return (int)lround(v * b->scale); };
    int  m      = S(12);
    int  bh     = S(28);
    int  bw     = S(96);
    int  lh     = S(20);
    int  w      = r.right;
    int  top    = m + lh + S(6);
    int  btnTop = r.bottom - m - bh;
    int  pathY  = btnTop - S(10) - lh;

    MoveWindow(b->header, m, m, w - (2 * m), lh, TRUE);
    MoveWindow(b->list, m, top, w - (2 * m), std::max(S(40), pathY - S(6) - top), TRUE);
    MoveWindow(b->path, m, pathY, w - (2 * m), lh, TRUE);
    MoveWindow(b->browse, m, btnTop, S(130), bh, TRUE);
    MoveWindow(b->cancel, w - m - bw, btnTop, bw, bh, TRUE);
    MoveWindow(b->open, w - m - (2 * bw) - S(8), btnTop, bw, bh, TRUE);
}

static PluginEntry * browser_current(Browser * b) {
    LRESULT sel = SendMessageW(b->list, LB_GETCURSEL, 0, 0);

    if (sel == LB_ERR) {
        return nullptr;
    }
    size_t i = (size_t)SendMessageW(b->list, LB_GETITEMDATA, (WPARAM)sel, 0);

    return (i < b->entries.size()) ? &b->entries[i] : nullptr;
}

static void browser_selected(Browser * b) {
    PluginEntry * e = browser_current(b);

    SetWindowTextW(b->path, (e != nullptr) ? e->path.c_str() : L"");
    EnableWindow(b->open, ((e != nullptr) && e->problem.empty()) ? TRUE : FALSE);
}

static void browser_open(Browser * b) {
    PluginEntry * e = browser_current(b);

    if ((e != nullptr) && e->problem.empty()) {
        b->chosen = e->path;
        b->done   = true;
    }
}

static void browser_browse(Browser * b) {
    wchar_t        file[MAX_PATH * 2] = {0};
    OPENFILENAMEW  ofn                = {};
    std::wstring   common             = env_dir(L"CommonProgramFiles", L"\\VST3");

    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = b->wnd;
    ofn.lpstrFilter     = L"VST3 plug-ins (*.vst3)\0*.vst3\0All files (*.*)\0*.*\0";
    ofn.lpstrFile       = file;
    ofn.nMaxFile        = MAX_PATH * 2;
    ofn.lpstrInitialDir = common.empty() ? nullptr : common.c_str();
    ofn.lpstrTitle      = L"Open a VST3 plug-in (inside a bundle: Contents\\<arch>-win\\<name>.vst3)";
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (!GetOpenFileNameW(&ofn)) {
        return;
    }
    int m = pe_machine(file);

    if (m != kOwnMachine) {
        std::wstring msg = (m < 0) ? std::wstring(L"That file is not a plug-in DLL.")
                                   : std::wstring(L"That plug-in is built for ") + machine_name(m) + L"; this host is " + kOwnArch + L".";

        MessageBoxW(b->wnd, msg.c_str(), L"vst3hostwin", MB_OK | MB_ICONWARNING);
        return;
    }
    b->chosen = file;
    b->done   = true;
}

static LRESULT CALLBACK browser_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    Browser * b = gBrowser;

    switch (msg) {
        case WM_SIZE:
            if ((b != nullptr) && (b->list != nullptr)) {
                browser_layout(b);
            }
            return 0;

        case WM_GETMINMAXINFO: {
            MINMAXINFO * mm = (MINMAXINFO *)lParam;
            double       s  = (b != nullptr) ? b->scale : 1.0;

            mm->ptMinTrackSize.x = (LONG)(480 * s);
            mm->ptMinTrackSize.y = (LONG)(300 * s);
            return 0;
        }

        case WM_COMMAND: {
            int id   = (int)LOWORD(wParam);
            int code = (int)HIWORD(wParam);

            if (b == nullptr) {
                return 0;
            }
            if ((id == kIdList) && (code == LBN_SELCHANGE)) {
                browser_selected(b);
            } else if (((id == kIdList) && (code == LBN_DBLCLK)) || (id == IDOK)) {
                browser_open(b);
            } else if (id == kIdBrowse) {
                browser_browse(b);
            } else if (id == IDCANCEL) {
                b->done = true;
            }
            return 0;
        }

        case WM_CLOSE:
            if (b != nullptr) {
                b->done = true;
            }
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static std::wstring choose_plugin(HWND owner) {
    static bool registered = false;
    HINSTANCE   inst       = GetModuleHandleW(nullptr);

    if (!registered) {
        WNDCLASSEXW wc = {};

        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = browser_proc;
        wc.hInstance     = inst;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"vst3hostwin-browser";
        RegisterClassExW(&wc);
        registered = true;
    }
    Browser b;

    b.entries = collect_plugins();
    b.wnd     = CreateWindowExW(0, L"vst3hostwin-browser", L"vst3hostwin - choose a plug-in",
                                WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100,
                                owner, nullptr, inst, nullptr);
    gBrowser  = &b;
    b.scale   = window_scale(b.wnd);
    b.font    = CreateFontW(-(int)lround(9.0 * 96.0 * b.scale / 72.0), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH, L"Segoe UI");

    auto child = [&](const wchar_t * cls, const wchar_t * text, DWORD style, DWORD ex, int id) {
        HWND h = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, b.wnd,
                                 (HMENU)(INT_PTR)id, inst, nullptr);

        SendMessageW(h, WM_SETFONT, (WPARAM)b.font, TRUE);
        return h;
    };
    b.header = child(L"STATIC", b.entries.empty() ? L"No plug-ins found here - Browse... to open a .vst3 file."
                                                  : L"Choose a plug-in, then Open (or double-click it).",
                     SS_LEFT, 0, kIdHeader);
    b.list   = child(L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | LBS_USETABSTOPS,
                     WS_EX_CLIENTEDGE, kIdList);
    b.path   = child(L"STATIC", L"", SS_LEFT | SS_PATHELLIPSIS | SS_NOPREFIX, 0, kIdPath);
    b.browse = child(L"BUTTON", L"&Browse...", WS_TABSTOP | BS_PUSHBUTTON, 0, kIdBrowse);
    b.open   = child(L"BUTTON", L"&Open", WS_TABSTOP | BS_DEFPUSHBUTTON, 0, IDOK);
    b.cancel = child(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, 0, IDCANCEL);

    int tabs[2] = {80, 150};    // dialog units: name | where | date

    SendMessageW(b.list, LB_SETTABSTOPS, 2, (LPARAM)tabs);
    int firstUsable = -1;

    for (size_t i = 0; i < b.entries.size(); i++) {
        const PluginEntry & e    = b.entries[i];
        std::wstring        text = e.name + L"\t" + e.where + L"\t" + e.date;

        if (!e.problem.empty()) {
            text += L"   (" + e.problem + L")";
        }
        LRESULT row = SendMessageW(b.list, LB_ADDSTRING, 0, (LPARAM)text.c_str());

        SendMessageW(b.list, LB_SETITEMDATA, (WPARAM)row, (LPARAM)i);

        if ((firstUsable < 0) && e.problem.empty()) {
            firstUsable = (int)row;
        }
    }
    SendMessageW(b.list, LB_SETCURSEL, (WPARAM)((firstUsable >= 0) ? firstUsable : 0), 0);
    browser_selected(&b);

    set_client_size_of(b.wnd, (int)lround(720 * b.scale), (int)lround(440 * b.scale), false);
    browser_layout(&b);

    // Centred on the plug-in's window, or on the screen
    RECT      wr;
    RECT      area;
    HMONITOR  mon = MonitorFromWindow((owner != nullptr) ? owner : b.wnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = {};

    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    GetWindowRect(b.wnd, &wr);

    if (owner != nullptr) {
        GetWindowRect(owner, &area);
    } else {
        area = mi.rcWork;
    }
    int x = area.left + ((area.right - area.left) - (wr.right - wr.left)) / 2;
    int y = area.top + ((area.bottom - area.top) - (wr.bottom - wr.top)) / 2;

    SetWindowPos(b.wnd, nullptr, std::max((int)mi.rcWork.left, x), std::max((int)mi.rcWork.top, y), 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);

    if (owner != nullptr) {
        EnableWindow(owner, FALSE);
    }
    ShowWindow(b.wnd, SW_SHOW);
    SetForegroundWindow(b.wnd);
    SetFocus(b.list);

    while (!b.done) {
        MSG m;
        int r = GetMessageW(&m, nullptr, 0, 0);

        if (r <= 0) {
            if (r == 0) {
                PostQuitMessage((int)m.wParam);
            }
            break;
        }

        if (!IsDialogMessageW(b.wnd, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    if (owner != nullptr) {
        EnableWindow(owner, TRUE);    // before the browser goes, so the activation returns to it
    }
    DestroyWindow(b.wnd);
    DeleteObject(b.font);
    gBrowser = nullptr;
    return b.chosen;
}

// ------------------------------------------------------------------------------------------------
// The menu bar: MIDI input, another plug-in, help
// ------------------------------------------------------------------------------------------------

static HMENU   gMidiMenu = nullptr;
static HMIDIIN gMidi     = nullptr;
static int     gMidiOpen = -1;

static std::wstring midi_name(int i) {
    MIDIINCAPSW caps = {};

    midiInGetDevCapsW((UINT)i, &caps, sizeof(caps));
    return caps.szPname;
}

static bool open_midi(int index) {
    if (gMidi != nullptr) {
        midiInStop(gMidi);
        midiInClose(gMidi);
        gMidi = nullptr;
    }
    gMidiOpen = -1;

    if (index >= 0) {
        if (midiInOpen(&gMidi, (UINT)index, (DWORD_PTR)midi_in, 0, CALLBACK_FUNCTION) == MMSYSERR_NOERROR) {
            midiInStart(gMidi);
            gMidiOpen = index;
            printf("midi: %ls open\n", midi_name(index).c_str());
        } else {
            gMidi = nullptr;
            fail("Could not open MIDI input %d (%s). Another program may have it open.", index, narrow(midi_name(index)).c_str());
        }
    }

    if (gMidiMenu != nullptr) {
        int count = GetMenuItemCount(gMidiMenu);

        CheckMenuItem(gMidiMenu, kMenuMidiNone, MF_BYCOMMAND | ((gMidiOpen < 0) ? MF_CHECKED : MF_UNCHECKED));

        for (int i = 0; i < count; i++) {
            CheckMenuItem(gMidiMenu, (UINT)(kMenuMidiFirst + i), MF_BYCOMMAND | ((i == gMidiOpen) ? MF_CHECKED : MF_UNCHECKED));
        }
    }
    return (gMidiOpen == index);
}

static void midi_menu_chosen(int item) {
    if (open_midi(item) || (item < 0)) {
        write_file(settings_dir() + L"\\midi.txt", (item >= 0) ? narrow(midi_name(item)) : std::string("-"));
    }
}

// The input chosen from the menu last time, found again by name - the numbers move when devices come and go
static int remembered_midi(void) {
    std::string name;

    if (!read_file(settings_dir() + L"\\midi.txt", name)) {
        return -1;
    }
    UINT ins = midiInGetNumDevs();

    for (UINT i = 0; i < ins; i++) {
        if (narrow(midi_name((int)i)) == name) {
            return (int)i;
        }
    }
    return -1;
}

static HMENU gKeysMenu = nullptr;

static void keys_menu_chosen(void) {
    gKeysPlay = !gKeysPlay;

    if (!gKeysPlay) {
        release_keys();
    }
    CheckMenuItem(gKeysMenu, kMenuKeys, MF_BYCOMMAND | (gKeysPlay ? MF_CHECKED : MF_UNCHECKED));
    write_file(settings_dir() + L"\\keys.txt", gKeysPlay ? "play" : "type");
    update_title();
}

static void build_menu(HWND hwnd) {
    HMENU bar  = CreateMenu();
    HMENU plug = CreatePopupMenu();
    HMENU help = CreatePopupMenu();
    std::string keys;

    if (read_file(settings_dir() + L"\\keys.txt", keys)) {
        gKeysPlay = (keys != "type");
    }
    gKeysMenu = CreatePopupMenu();
    AppendMenuW(gKeysMenu, MF_STRING | (gKeysPlay ? MF_CHECKED : 0u), kMenuKeys, L"&Letters play notes (Caps Lock on: type instead)");
    AppendMenuW(gKeysMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(gKeysMenu, MF_STRING, kMenuOctaveDown, L"Octave &down\tZ");
    AppendMenuW(gKeysMenu, MF_STRING, kMenuOctaveUp, L"Octave &up\tX");

    AppendMenuW(plug, MF_STRING, kMenuOpen, L"&Open another plug-in...");
    AppendMenuW(plug, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(plug, MF_STRING, kMenuExit, L"E&xit");

    gMidiMenu = CreatePopupMenu();
    AppendMenuW(gMidiMenu, MF_STRING, kMenuMidiNone, L"&None");
    UINT ins = midiInGetNumDevs();

    if (ins > 0) {
        AppendMenuW(gMidiMenu, MF_SEPARATOR, 0, nullptr);
    }
    for (UINT i = 0; i < ins; i++) {
        AppendMenuW(gMidiMenu, MF_STRING, (UINT_PTR)(kMenuMidiFirst + (int)i), midi_name((int)i).c_str());
    }
    AppendMenuW(help, MF_STRING, kMenuHelp, L"&Playing notes...");

    AppendMenuW(bar, MF_POPUP, (UINT_PTR)plug, L"&Plug-in");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)gKeysMenu, L"&Keyboard");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)gMidiMenu, L"&MIDI input");
    AppendMenuW(bar, MF_POPUP, (UINT_PTR)help, L"&Help");
    SetMenu(hwnd, bar);
}

static void update_title(void) {
    if (gWindow == nullptr) {
        return;
    }
    wchar_t t[512];

    if (!gKeysPlay) {
        swprintf(t, 512, L"%ls - vst3hostwin      letters type into the plug-in (Keyboard menu to play notes)", gPluginName.c_str());
    } else if (caps_lock_on()) {
        swprintf(t, 512, L"%ls - vst3hostwin      Caps Lock is on: letters type into the plug-in", gPluginName.c_str());
    } else {
        swprintf(t, 512, L"%ls - vst3hostwin      A-L white notes from C%d, W E T Y U O P black, Z / X octave",
                 gPluginName.c_str(), gOctave);
    }
    SetWindowTextW(gWindow, t);
}

static void show_help(HWND owner) {
    std::wstring text =
        L"Playing notes from the computer keyboard\n\n"
        L"The letter keys play notes, as in G2Alike:\n"
        L"    A S D F G H J K L  -  the white keys, from C\n"
        L"    W E  T Y U  O P     -  the black keys between them\n"
        L"    Z / X  -  octave down / up (the title bar shows which C the A key plays)\n\n"
        L"To type into the plug-in itself (a patch name, its file browser), turn Caps Lock ON,\n"
        L"or untick Keyboard > Letters play notes. Keys held with Ctrl, Alt or Win are never notes.\n\n"
        L"A MIDI keyboard: choose it from the MIDI input menu; the choice is remembered.\n\n"
        L"Each plug-in's state - the patch you loaded through its own menu - is saved when\n"
        L"you close the window and comes back next time. It is kept in\n    " + settings_dir();

    MessageBoxW(owner, text.c_str(), L"vst3hostwin", MB_OK | MB_ICONINFORMATION);
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
    // notes §9: started from an icon, the console Windows gave us is ours alone - hide it
    {
        DWORD ids[2];

        if (GetConsoleProcessList(ids, 2) <= 1) {
            HWND console = GetConsoleWindow();

            if (console != nullptr) {
                ShowWindow(console, SW_HIDE);
            }
            FreeConsole();
            gHaveConsole = false;
        }
    }
    int       wargc = 0;
    LPWSTR *  wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);    // the path as typed, not the ANSI copy
    std::wstring pluginW;
    const char * shotPath   = nullptr;
    double       seconds    = 0.0;
    int          midiIndex  = -1;
    int          audioIndex = 0;
    int          heldNote   = -1;
    bool         dpiAware   = true;
    bool         audio      = true;
    bool         freshState    = false;
    bool         readOnlyState = false;

    for (int i = 1; i < argc; i++) {
        if ((argv[i][0] != '-') && pluginW.empty()) {
            pluginW = ((wargv != nullptr) && (wargc == argc)) ? std::wstring(wargv[i]) : widen(argv[i]);
        } else if ((strcmp(argv[i], "--seconds") == 0) && (i + 1 < argc)) {
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

    if (pluginW.empty()) {
        pluginW = choose_plugin(nullptr);

        if (pluginW.empty()) {
            return 0;
        }
    }
    {
        wchar_t full[MAX_PATH * 2];
        DWORD   n = GetFullPathNameW(pluginW.c_str(), MAX_PATH * 2, full, nullptr);

        if ((n > 0) && (n < MAX_PATH * 2)) {
            pluginW = full;    // the recent list and a relaunch must not depend on where it was started from
        }
    }
    gPluginName = leaf_of(pluginW);

    if (is_vst3_name(gPluginName)) {
        gPluginName.resize(gPluginName.size() - 5);
    }

    // --- the DLL, its factory, the component and controller --------------------------------------
    std::wstring binary  = binary_path(pluginW);
    int          machine = pe_machine(binary);

    if ((machine >= 0) && (machine != kOwnMachine)) {
        fail("%s is built for %s, and this host is %s.%s", narrow(gPluginName).c_str(), narrow(machine_name(machine)).c_str(),
             narrow(kOwnArch).c_str(), (machine == 0x8664) ? " Use vst3hostwin-x64.exe for it." : "");
        return 1;
    }
    HMODULE dll = LoadLibraryW(binary.c_str());

    if (dll == nullptr) {
        fail("LoadLibrary failed (error %lu):\n%s", GetLastError(), narrow(binary).c_str());
        return 1;
    }
    typedef bool (*tInitDll)(void);
    typedef bool (*tExitDll)(void);
    typedef IPluginFactory * (*tGetFactory)(void);
    tInitDll    initDll    = (tInitDll)(void *)GetProcAddress(dll, "InitDll");
    tExitDll    exitDll    = (tExitDll)(void *)GetProcAddress(dll, "ExitDll");
    tGetFactory getFactory = (tGetFactory)(void *)GetProcAddress(dll, "GetPluginFactory");

    if (getFactory == nullptr) {
        fail("%s is not a VST3 plug-in (no GetPluginFactory export).", narrow(binary).c_str());
        return 1;
    }

    if (initDll != nullptr) {
        initDll();
    }
    IPluginFactory * factory = getFactory();

    if (factory == nullptr) {
        fail("%s: GetPluginFactory returned null.", narrow(gPluginName).c_str());
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
        fail("%s: no audio class %d, or it could not be created.", narrow(gPluginName).c_str(), audioIndex);
        return 1;
    }
    gComponent->initialize(&gHostApp);
    remember_recent(pluginW);

    // The controller the component names - not whichever the factory listed last
    if (gComponent->getControllerClassId(controllerCid) == kResultTrue) {
        haveController = true;
    }

    if (!haveController
       || (factory->createInstance(controllerCid, IEditController::iid, (void **)&gController) != kResultTrue)
       || (gController == nullptr)) {
        fail("%s: no controller, or it could not be created.", narrow(gPluginName).c_str());
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
            fail("No audio output device - the plug-in will be silent.");
        }
    }

    // --- MIDI: --midi N, else the input chosen from the menu last time ---------------------------------
    UINT ins = midiInGetNumDevs();

    for (UINT i = 0; i < ins; i++) {
        printf("midi in %u: %ls\n", i, midi_name((int)i).c_str());
    }

    if (midiIndex < 0) {
        midiIndex = remembered_midi();
    }

    // --- the editor ----------------------------------------------------------------------------------
    WNDCLASSEXW wc = {};

    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = host_proc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.lpszClassName = L"vst3hostwin";
    RegisterClassExW(&wc);

    gWindow = CreateWindowExW(0, wc.lpszClassName, L"vst3hostwin", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, 800, 600, nullptr, nullptr, wc.hInstance, nullptr);
    build_menu(gWindow);    // before the view's size is applied, so the client area allows for it
    update_title();
    open_midi(midiIndex);
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

    open_midi(-1);

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

    // notes §11: the plug-in chosen from the menu, in a fresh process once this one has saved its state
    if (!gRelaunch.empty()) {
        wchar_t exe[MAX_PATH] = {0};

        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring        cmd = L"\"" + std::wstring(exe) + L"\" \"" + gRelaunch + L"\"" + (dpiAware ? L"" : L" --dpi-unaware");
        STARTUPINFOW        si  = {};
        PROCESS_INFORMATION pi  = {};

        si.cb = sizeof(si);

        if (CreateProcessW(exe, &cmd[0], nullptr, nullptr, FALSE, gHaveConsole ? 0u : (DWORD)DETACHED_PROCESS,
                           nullptr, nullptr, &si, &pi)) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            fail("Could not start a new vst3hostwin for %s.", narrow(gRelaunch).c_str());
        }
    }
    return 0;
}
