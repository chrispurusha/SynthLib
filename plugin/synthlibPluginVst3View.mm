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
// Notes: Docs/code-notes/synthlibPluginVst3View.mm.md - "// notes §k" refers there.

// notes §1

#import <Cocoa/Cocoa.h>

#include <atomic>
#include <cmath>
#include <vector>

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/gui/iplugview.h"

#include "synthlibPlugin.h"
#include "synthlibPluginVst3View.h"

using namespace Steinberg;

class SynthLibVst3View;

// notes §2
static std::vector<SynthLibVst3View *> gOpenViews;

class SynthLibVst3View : public IPlugView {
public:
    SynthLibVst3View(const tSynthLibPluginDesc * descriptor, void * pluginInstance, FUnknown * ownerIn,
                     double initialWidth, double * widthSinkIn)
        : refCount(1), desc(descriptor), inst(pluginInstance), owner(ownerIn), widthSink(widthSinkIn) {
        const tSynthLibPluginDesc * d = desc;

        // THE OWNER OUTLIVES THE VIEW, which is what makes widthSink safe to write through: it points
        // into the controller, and a host is free to release the controller before the view.
        if (owner != nullptr) {
            owner->addRef();
        }

        // notes §3
        double width = d->editorDefaultWidth;

        if (initialWidth > 0.0) {
            width = initialWidth;
        } else if (d->editorWidthLoad != nullptr) {
            double saved = (double)d->editorWidthLoad();

            if (saved >= d->editorMinWidth) {
                width = saved;
            }
        }
        currentWidth  = clamp_width(width);
        currentHeight = height_for(currentWidth);
    }

    virtual ~SynthLibVst3View(void) {
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
            return false;       // no channel to the host; the caller must leave the window alone
        }
        ViewRect rect = {};

        rect.left   = 0;
        rect.top    = 0;
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
        const tSynthLibPluginDesc * d = desc;

        if (d->cb.createView == nullptr) {
            return kResultFalse;
        }
        NSView * host = (__bridge NSView *)parent;

        editorView = (__bridge_transfer NSView *)d->cb.createView(d, inst, currentWidth, currentHeight);

        if (editorView == nil) {
            return kResultFalse;    // better to fail than to show the host an empty window
        }

        // notes §4
        [editorView setFrame:NSMakeRect(0.0, 0.0, currentWidth, currentHeight)];
        [editorView setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
        [host addSubview:editorView];
        forget();
        gOpenViews.push_back(this);
        return kResultOk;
    }

    tresult PLUGIN_API removed(void) SMTG_OVERRIDE {
        // TAKEN OUT OF THE HIERARCHY BEFORE THE PLUG-IN IS TOLD. The view owns a drawing surface
        // bound to this window, and tearing the window down around a live one is the sort of thing
        // that works everywhere except the host somebody reports it from.
        if (editorView != nil) {
            [editorView removeFromSuperview];

            if (desc->cb.destroyView != nullptr) {
                desc->cb.destroyView(inst, (__bridge void *)editorView);
            }
            editorView = nil;
        }
        forget();
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

        // notes §5
        if (widthSink != nullptr) {
            *widthSink = currentWidth;
        }

        if (desc->editorWidthSave != nullptr) {
            desc->editorWidthSave((long)currentWidth);
        }

        // ONLY WHEN IT DISAGREES. The autoresizing mask has usually put the view here already, and
        // setting a frame it already has still runs a layout pass.
        if ((editorView != nil) &&
            (NSEqualRects([editorView frame], NSMakeRect(0.0, 0.0, currentWidth, currentHeight)) == NO)) {
            [editorView setFrame:NSMakeRect(0.0, 0.0, currentWidth, currentHeight)];
        }

        if (editorView != nil) {

            if (desc->cb.viewResized != nullptr) {
                desc->cb.viewResized(inst, (__bridge void *)editorView, currentWidth, currentHeight);
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

    // notes §6
    tresult PLUGIN_API checkSizeConstraint(ViewRect * rect) SMTG_OVERRIDE {
        if (rect == nullptr) {
            return kInvalidArgument;
        }
        const tSynthLibPluginDesc * d      = desc;
        double                      wanted = (double)(rect->right - rect->left);

        if (d->editorAspect > 0.0) {
            double fromHeight = (double)(rect->bottom - rect->top) * d->editorAspect;

            wanted = (wanted + fromHeight) * 0.5;
        }
        wanted      = clamp_width(wanted);
        rect->right = rect->left + (int32)lround(wanted);

        if (d->editorAspect > 0.0) {
            rect->bottom = rect->top + (int32)lround(wanted / d->editorAspect);
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
        if (desc->editorAspect > 0.0) {
            return width / desc->editorAspect;
        }
        return width;       // free-resizing editors get a square default and the host's own frame after
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
    NSView * __strong  editorView    = nil;
    double             currentWidth  = 0.0;
    double             currentHeight = 0.0;
    IPlugFrame *       plugFrame     = nullptr;
};

IPlugView * synthlib_vst3_create_view(const tSynthLibPluginDesc * desc, void * inst, FUnknown * owner,
                                      double initialWidth, double * widthSink) {
    return new SynthLibVst3View(desc, inst, owner, initialWidth, widthSink);
}

// ------------------------------------------------------------------------------------------------

bool synthlib_plugin_request_resize(void * inst, double width, double height) {
    for (SynthLibVst3View * view : gOpenViews) {
        if (view->instance() == inst) {
            return view->request_resize(width, height);
        }
    }
    return false;           // no editor of this instance's open; nothing to resize
}
