/*
 * SynthLib - the VST3 editor window, written once for every plug-in in these projects.
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

// AN IPlugView AND NOTHING ELSE. The window's CONTENTS come from the plug-in's own createView(),
// which hands back an NSView; everything here is the protocol a VST3 host speaks to a window.
//
// The same NSView is what the Audio Unit wrapper's Cocoa view factory asks for, so a change to the
// editor is made once and both formats show it.

#import <Cocoa/Cocoa.h>

#include <atomic>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"

#include "synthlibPlugin.h"
#include "synthlibPluginVst3View.h"

using namespace Steinberg;

// The frame of the sole open editor, for synthlib_plugin_request_resize(). One editor at a time is
// the same limit the rest of this wrapper works under - see the instance registry in
// synthlibPluginVst3.cpp.
static std::atomic<IPlugFrame *> gPlugFrame{nullptr};
static std::atomic<IPlugView *>  gPlugView{nullptr};

static const tSynthLibPluginDesc * desc(void) {
    static const tSynthLibPluginDesc * d = synthlib_plugin_descriptor();

    return d;
}

class SynthLibVst3View : public IPlugView {
public:
    explicit SynthLibVst3View(void * pluginInstance) : refCount(1), inst(pluginInstance) {
        const tSynthLibPluginDesc * d = desc();

        // RESTORE THE SIZE IN THE CONSTRUCTOR, not in attached(). getSize() is asked BEFORE
        // attached(), so a width recovered any later opens the window at the default and then
        // jumps it.
        //
        // WIDTH ONLY IS STORED: with an aspect lock the height is derived from it, so keeping both
        // would be storing the same fact twice and inviting them to disagree.
        double width = d->editorDefaultWidth;

        if (d->editorWidthLoad != nullptr) {
            double saved = (double)d->editorWidthLoad();

            if (saved >= d->editorMinWidth) {
                width = saved;
            }
        }
        currentWidth  = width;
        currentHeight = height_for(width);
    }

    virtual ~SynthLibVst3View(void) {
        if (gPlugView.load() == this) {
            gPlugView.store(nullptr);
            gPlugFrame.store(nullptr);
        }
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

    // macOS hosts pass an NSView. Anything else - an HWND, an X11 window - is not something this
    // build can attach to, and saying so is what makes the host fall back gracefully rather than
    // hand over a pointer that would be misused.
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) SMTG_OVERRIDE {
        return (strcmp(type, kPlatformTypeNSView) == 0) ? kResultTrue : kResultFalse;
    }

    tresult PLUGIN_API attached(void * parent, FIDString type) SMTG_OVERRIDE {
        if ((parent == nullptr) || (isPlatformTypeSupported(type) != kResultTrue)) {
            return kResultFalse;
        }
        const tSynthLibPluginDesc * d = desc();

        if (d->cb.createView == nullptr) {
            return kResultFalse;
        }
        NSView * host = (__bridge NSView *)parent;

        editorView = (__bridge_transfer NSView *)d->cb.createView(inst, currentWidth, currentHeight);

        if (editorView == nil) {
            return kResultFalse;    // better to fail than to show the host an empty window
        }
        [host addSubview:editorView];
        gPlugView.store(this);
        return kResultOk;
    }

    tresult PLUGIN_API removed(void) SMTG_OVERRIDE {
        // TAKEN OUT OF THE HIERARCHY BEFORE THE PLUG-IN IS TOLD. The view owns a drawing surface
        // bound to this window, and tearing the window down around a live one is the sort of thing
        // that works everywhere except the host somebody reports it from.
        if (editorView != nil) {
            [editorView removeFromSuperview];

            if (desc()->cb.destroyView != nullptr) {
                desc()->cb.destroyView(inst, (__bridge void *)editorView);
            }
            editorView = nil;
        }
        gPlugView.store(nullptr);
        gPlugFrame.store(nullptr);
        return kResultOk;
    }

    // The host offering us keyboard and wheel events it caught first. Declined: the NSView is in
    // the responder chain and gets them directly, which is where the plug-in's own input handling
    // already reads them.
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

    // The host telling us the frame it has given the view. Resize to match, and RECORD it, so
    // getSize() reports what the host last set rather than the original default - Ableton in
    // particular asks again after resizing and will fight a stale answer.
    tresult PLUGIN_API onSize(ViewRect * newSize) SMTG_OVERRIDE {
        if (newSize == nullptr) {
            return kInvalidArgument;
        }
        currentWidth  = (double)(newSize->right - newSize->left);
        currentHeight = (double)(newSize->bottom - newSize->top);

        // Remembered for next time. Written on every resize rather than on close, because a host is
        // under no obligation to tell a view it is going away in any particular order.
        if (desc()->editorWidthSave != nullptr) {
            desc()->editorWidthSave((long)currentWidth);
        }

        if (editorView != nil) {
            [editorView setFrame:NSMakeRect(0.0, 0.0, currentWidth, currentHeight)];

            if (desc()->cb.viewResized != nullptr) {
                desc()->cb.viewResized(inst, (__bridge void *)editorView, currentWidth, currentHeight);
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
        gPlugFrame.store(frame);
        return kResultOk;
    }

    tresult PLUGIN_API canResize(void) SMTG_OVERRIDE {
        return (desc()->editorMinWidth < desc()->editorDefaultWidth) ? kResultTrue : kResultFalse;
    }

    // THE ASPECT RATIO IS LOCKED when the descriptor asks for it, as the canvas application locks
    // its own window with glfwSetWindowAspectRatio().
    //
    // That lock is what completes the scaling: a canvas scaled from WIDTH alone would, in a taller
    // window, simply uncover more rows rather than drawing larger. The application never shows that
    // because its window cannot be made taller without also becoming wider.
    //
    // Width is treated as the authority and height derived from it: a drag usually changes both, and
    // following the width matches how the application's own resize feels.
    tresult PLUGIN_API checkSizeConstraint(ViewRect * rect) SMTG_OVERRIDE {
        if (rect == nullptr) {
            return kInvalidArgument;
        }
        const tSynthLibPluginDesc * d     = desc();
        int32                       width = rect->right - rect->left;

        if (width < (int32)d->editorMinWidth) {
            width = (int32)d->editorMinWidth;
        }
        rect->right = rect->left + width;

        if (d->editorAspect > 0.0) {
            rect->bottom = rect->top + (int32)((double)width / d->editorAspect);
        }
        return kResultTrue;
    }

private:
    static double height_for(double width) {
        const tSynthLibPluginDesc * d = desc();

        if (d->editorAspect > 0.0) {
            return width / d->editorAspect;
        }
        return width;       // free-resizing editors get a square default and the host's own frame after
    }

    std::atomic<int32> refCount;
    void *             inst;
    NSView * __strong  editorView    = nil;
    double             currentWidth  = 0.0;
    double             currentHeight = 0.0;
    IPlugFrame *       plugFrame     = nullptr;
};

IPlugView * synthlib_vst3_create_view(void * inst) {
    return new SynthLibVst3View(inst);
}

// ------------------------------------------------------------------------------------------------

bool synthlib_plugin_request_resize(double width, double height) {
    IPlugFrame * frame = gPlugFrame.load();
    IPlugView *  view  = gPlugView.load();

    if ((frame == nullptr) || (view == nullptr)) {
        return false;       // no channel to the host; the caller must leave the window alone
    }
    ViewRect rect = {};

    rect.left   = 0;
    rect.top    = 0;
    rect.right  = (int32)width;
    rect.bottom = (int32)height;
    return (frame->resizeView(view, &rect) == kResultOk);
}
