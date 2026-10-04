/*
 * SynthLib - the VST3 editor window on Windows, the HWND counterpart to synthlibPluginVst3View.mm.
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
// Notes: Docs/code-notes/synthlibPluginVst3ViewWin.cpp.md - "// notes §k" refers there.

// notes §1

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"

#include "synthlibPlugin.h"
#include "synthlibPluginVst3View.h"

using namespace Steinberg;

class SynthLibVst3ViewWin;

static std::vector<SynthLibVst3ViewWin *> gOpenViews;

class SynthLibVst3ViewWin : public IPlugView {
public:
    SynthLibVst3ViewWin(const tSynthLibPluginDesc * descriptor, void * pluginInstance, FUnknown * ownerIn,
                        double initialWidth, double * widthSinkIn)
        : refCount(1), desc(descriptor), inst(pluginInstance), owner(ownerIn), widthSink(widthSinkIn) {
        if (owner != nullptr) {
            owner->addRef();
        }
        double width = desc->editorDefaultWidth;

        if (initialWidth > 0.0) {
            width = initialWidth;
        } else if (desc->editorWidthLoad != nullptr) {
            double saved = (double)desc->editorWidthLoad();

            if (saved >= desc->editorMinWidth) {
                width = saved;
            }
        }
        currentWidth  = clamp_width(width);
        currentHeight = height_for(currentWidth);
    }

    virtual ~SynthLibVst3ViewWin(void) {
        forget();

        if (owner != nullptr) {
            owner->release();
            owner = nullptr;
        }
    }

    void * instance(void) const {
        return inst;
    }

    bool request_resize(double width, double height) {
        if (plugFrame == nullptr) {
            return false;
        }
        ViewRect rect = {};

        rect.right  = (int32)width;
        rect.bottom = (int32)height;
        return (plugFrame->resizeView(this, &rect) == kResultOk);
    }

    tresult PLUGIN_API queryInterface(const TUID iid, void ** obj) SMTG_OVERRIDE {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IPlugView)
        QUERY_INTERFACE(iid, obj, IPlugView::iid, IPlugView)
        *obj = nullptr;
        return kNoInterface;
    }

    uint32 PLUGIN_API addRef(void) SMTG_OVERRIDE {
        return (uint32)++refCount;
    }

    uint32 PLUGIN_API release(void) SMTG_OVERRIDE {
        int32 c = --refCount;

        if (c == 0) {
            delete this;
            return 0;
        }
        return (uint32)c;
    }

    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE {
        return (strcmp(type, kPlatformTypeHWND) == 0) ? kResultTrue : kResultFalse;
    }

    // notes §2
    tresult PLUGIN_API attached(void * parent, FIDString type) SMTG_OVERRIDE {
        if ((parent == nullptr) || (isPlatformTypeSupported(type) != kResultTrue) || (desc->cb.createView == nullptr)) {
            return kResultFalse;
        }
        editorWindow = (HWND)desc->cb.createView(desc, inst, currentWidth, currentHeight);

        if (editorWindow == nullptr) {
            return kResultFalse;
        }
        LONG_PTR style = GetWindowLongPtrW(editorWindow, GWL_STYLE);

        style &= ~(LONG_PTR)(WS_POPUP | WS_CAPTION | WS_THICKFRAME);
        style |= (LONG_PTR)(WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        SetWindowLongPtrW(editorWindow, GWL_STYLE, style);
        SetParent(editorWindow, (HWND)parent);
        SetWindowPos(editorWindow, nullptr, 0, 0, (int)currentWidth, (int)currentHeight,
                     SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        forget();
        gOpenViews.push_back(this);
        return kResultOk;
    }

    tresult PLUGIN_API removed(void) SMTG_OVERRIDE {
        if (editorWindow != nullptr) {
            ShowWindow(editorWindow, SW_HIDE);
            SetParent(editorWindow, nullptr);

            if (desc->cb.destroyView != nullptr) {
                desc->cb.destroyView(inst, (void *)editorWindow);   // the plug-in destroys its own window
            }
            editorWindow = nullptr;
        }
        forget();
        return kResultOk;
    }

    // Declined: the editor window has the keyboard focus after a click and reads keys itself
    tresult PLUGIN_API onWheel(float distance) SMTG_OVERRIDE {
        (void)distance;
        return kResultFalse;
    }

    tresult PLUGIN_API onKeyDown(char16 key, int16 code, int16 mods) SMTG_OVERRIDE {
        (void)key;
        (void)code;
        (void)mods;
        return kResultFalse;
    }

    tresult PLUGIN_API onKeyUp(char16 key, int16 code, int16 mods) SMTG_OVERRIDE {
        (void)key;
        (void)code;
        (void)mods;
        return kResultFalse;
    }

    tresult PLUGIN_API getSize(ViewRect * size) SMTG_OVERRIDE {
        if (size == nullptr) {
            return kInvalidArgument;
        }
        size->left   = 0;
        size->top    = 0;
        size->right  = (int32)currentWidth;
        size->bottom = (int32)currentHeight;
        return kResultOk;
    }

    tresult PLUGIN_API onSize(ViewRect * newSize) SMTG_OVERRIDE {
        if (newSize == nullptr) {
            return kInvalidArgument;
        }
        currentWidth  = (double)(newSize->right - newSize->left);
        currentHeight = (double)(newSize->bottom - newSize->top);

        if (widthSink != nullptr) {
            *widthSink = currentWidth;
        }

        if (desc->editorWidthSave != nullptr) {
            desc->editorWidthSave((long)currentWidth);
        }

        if (editorWindow != nullptr) {
            SetWindowPos(editorWindow, nullptr, 0, 0, (int)currentWidth, (int)currentHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);

            if (desc->cb.viewResized != nullptr) {
                desc->cb.viewResized(inst, (void *)editorWindow, currentWidth, currentHeight);
            }
        }
        return kResultOk;
    }

    tresult PLUGIN_API onFocus(TBool state) SMTG_OVERRIDE {
        (void)state;
        return kResultOk;
    }

    tresult PLUGIN_API setFrame(IPlugFrame * frame) SMTG_OVERRIDE {
        plugFrame = frame;
        return kResultOk;
    }

    tresult PLUGIN_API canResize(void) SMTG_OVERRIDE {
        return (desc->editorMinWidth < desc->editorDefaultWidth) ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API checkSizeConstraint(ViewRect * rect) SMTG_OVERRIDE {
        if (rect == nullptr) {
            return kInvalidArgument;
        }
        double wanted = (double)(rect->right - rect->left);

        if (desc->editorAspect > 0.0) {
            double fromHeight = (double)(rect->bottom - rect->top) * desc->editorAspect;

            wanted = (wanted + fromHeight) * 0.5;
        }
        wanted      = clamp_width(wanted);
        rect->right = rect->left + (int32)lround(wanted);

        if (desc->editorAspect > 0.0) {
            rect->bottom = rect->top + (int32)lround(wanted / desc->editorAspect);
        }
        return kResultTrue;
    }

private:
    void forget(void) {
        for (size_t i = 0; i < gOpenViews.size(); i++) {
            if (gOpenViews[i] == this) {
                gOpenViews.erase(gOpenViews.begin() + (long)i);
                return;
            }
        }
    }

    double height_for(double width) const {
        return (desc->editorAspect > 0.0) ? (width / desc->editorAspect) : width;
    }

    double clamp_width(double width) const {
        if (width < desc->editorMinWidth) {
            width = desc->editorMinWidth;
        }

        if ((desc->editorMaxWidth > 0.0) && (width > desc->editorMaxWidth)) {
            width = desc->editorMaxWidth;
        }
        return width;
    }

    std::atomic<int32>          refCount;
    const tSynthLibPluginDesc * desc;
    void *                      inst;
    FUnknown *                  owner;
    double *                    widthSink;
    HWND                        editorWindow  = nullptr;
    double                      currentWidth  = 0.0;
    double                      currentHeight = 0.0;
    IPlugFrame *                plugFrame     = nullptr;
};

IPlugView * synthlib_vst3_create_view(const tSynthLibPluginDesc * desc, void * inst, FUnknown * owner,
                                      double initialWidth, double * widthSink) {
    return new SynthLibVst3ViewWin(desc, inst, owner, initialWidth, widthSink);
}

bool synthlib_plugin_request_resize(void * inst, double width, double height) {
    for (SynthLibVst3ViewWin * view : gOpenViews) {
        if (view->instance() == inst) {
            return view->request_resize(width, height);
        }
    }
    return false;
}
