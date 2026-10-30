/*
 * SynthLib - the Audio Unit's Cocoa editor view, written once for every plug-in in these projects.
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
// Notes: Docs/code-notes/synthlibPluginAuView.m.md - "// notes §k" refers there.

// notes §1

#import <Cocoa/Cocoa.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <AudioUnit/AudioUnit.h>

#include "synthlibPlugin.h"
#include "synthlibPluginAu.h"
#include "synthlibAuViewFactory.h"
#include "synthlibRuntimeClass.h"

// Only for the things that are true of the whole BINARY rather than of one plug-in in it - the view
// class name, and the description string. Anything per-plug-in comes from the handle the AudioUnit
// hands back; see uiViewForAudioUnit:.
static const tSynthLibPluginDesc * first_variant(void) {
    return &synthlib_plugin_variants()->variants[0];
}

static CFStringRef gViewClassName = NULL;

void synthlib_au_set_view_class_name(const char * name) {
    gViewClassName = CFStringCreateWithCString(NULL, name, kCFStringEncodingUTF8);
}

CFStringRef synthlib_au_view_class_name(void) {
    return gViewClassName;      // +0; the caller copies it if it wants to keep it
}

// ------------------------------------------------------------------------------------------------
// The container
// ------------------------------------------------------------------------------------------------

// notes §2
// A run-time class - see synthlibRuntimeClass.h.
typedef struct {
    const tSynthLibPluginDesc * pluginDesc;
    void *                      pluginInstance;
} tContainerState;

static Class gContainerClass = Nil;
static char  gEditorKey      = 0;

static tContainerState * container_state(NSView * self) {
    return (tContainerState *)synthlib_runtime_class_state(gContainerClass, self);
}

static NSView * container_editor(NSView * self) {
    return objc_getAssociatedObject(self, &gEditorKey);
}

static void container_set_editor(NSView * self, NSView * editor) {
    objc_setAssociatedObject(self, &gEditorKey, editor, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void container_setFrameSize(NSView * self, SEL _cmd, NSSize newSize) {
    const tSynthLibPluginDesc * d = container_state(self)->pluginDesc;

    if (d == NULL) {
        SYNTHLIB_SUPER_ARG(self, _cmd, NSSize, newSize);
        return;
    }

    if (newSize.width < d->editorMinWidth) {
        newSize.width = d->editorMinWidth;
    }

    if ((d->editorMaxWidth > 0.0) && (newSize.width > d->editorMaxWidth)) {
        newSize.width = d->editorMaxWidth;
    }

    if (d->editorAspect > 0.0) {
        newSize.height = newSize.width / d->editorAspect;
    }
    SYNTHLIB_SUPER_ARG(self, _cmd, NSSize, newSize);

    if (container_editor(self) != nil) {
        [container_editor(self) setFrame:NSMakeRect(0.0, 0.0, newSize.width, newSize.height)];

        if (d->cb.viewResized != NULL) {
            d->cb.viewResized(container_state(self)->pluginInstance, (__bridge void *)container_editor(self),
                              newSize.width, newSize.height);
        }
    }

    // Remembered for next time, and through the SAME preference the VST3 editor writes - so an
    // editor opened in Logic and one opened in Live agree about how big it should be.
    if (d->editorWidthSave != NULL) {
        d->editorWidthSave((long)newSize.width);
    }
}

// The host is taking the window away.
static void container_viewWillMoveToSuperview(NSView * self, SEL _cmd, NSView * newSuperview) {
    const tSynthLibPluginDesc * d = container_state(self)->pluginDesc;

    if ((newSuperview == nil) && (container_editor(self) != nil) && (d != NULL)) {
        if (d->cb.destroyView != NULL) {
            d->cb.destroyView(container_state(self)->pluginInstance, (__bridge void *)container_editor(self));
        }
        [container_editor(self) removeFromSuperview];
        container_set_editor(self, nil);
    }
    SYNTHLIB_SUPER_ARG(self, _cmd, NSView *, newSuperview);
}

static void container_make_class(void) {
    Class cls = synthlib_runtime_class_begin([NSView class], "SynthLibAuContainer", sizeof(tContainerState),
                                             (const void *)&gContainerClass);

    synthlib_runtime_class_override(cls, @selector(setFrameSize:), container_setFrameSize);
    synthlib_runtime_class_override(cls, @selector(viewWillMoveToSuperview:), container_viewWillMoveToSuperview);
    objc_registerClassPair(cls);
    gContainerClass = cls;
}

// ------------------------------------------------------------------------------------------------
// What the factory class forwards to - see synthlibAuViewFactory.h
// ------------------------------------------------------------------------------------------------

NSString * synthlib_au_view_description(void) {
    return [NSString stringWithUTF8String:first_variant()->name];
}

NSView * synthlib_au_view_create(AudioUnit inAU, NSSize inPreferredSize) {
    static dispatch_once_t      once      = 0;

    dispatch_once(&once, ^{
        container_make_class();
    });

    // BACK FROM AN AudioUnit TO OUR OWN INSTANCE. A Cocoa view factory is handed the AudioUnit and
    // nothing else, so the wrapper publishes both the instance and WHICH PLUG-IN IT IS as a private
    // property, and this is the only way across - see kSynthLibAuProperty_Instance.
    tSynthLibAuHandle           handle    = {NULL, NULL};
    UInt32                      size      = (UInt32)sizeof(handle);
    OSStatus                    err       = AudioUnitGetProperty(inAU, kSynthLibAuProperty_Instance,
                                                                 kAudioUnitScope_Global, 0, &handle, &size);

    if ((err != noErr) || (handle.desc == NULL) || (handle.inst == NULL)) {
        return nil;
    }
    const tSynthLibPluginDesc * d         = handle.desc;
    void *                      inst      = handle.inst;

    if (d->cb.createView == NULL) {
        return nil;
    }
    // The host's preferred size is usually zero, meaning "whatever you like". The remembered width
    // wins over the default when there is one, exactly as it does on the VST3 side.
    double                      width     = d->editorDefaultWidth;

    if (d->editorWidthLoad != NULL) {
        double saved = (double)d->editorWidthLoad();

        if (saved >= d->editorMinWidth) {
            width = saved;
        }
    }

    if (inPreferredSize.width >= d->editorMinWidth) {
        width = inPreferredSize.width;
    }

    if ((d->editorMaxWidth > 0.0) && (width > d->editorMaxWidth)) {
        width = d->editorMaxWidth;
    }
    double                      height    = (d->editorAspect > 0.0) ? (width / d->editorAspect) : width;

    // createView() hands its view back RETAINED - see the note in synthlibPlugin.h - so the bridge
    // that takes ownership is __bridge_transfer, and ARC releases it with the container.
    NSView *                    editor    = (__bridge_transfer NSView *)d->cb.createView(d, inst, width, height);

    if (editor == nil) {
        return nil;
    }
    NSView *                    container = [[gContainerClass alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, height)];

    container_state(container)->pluginDesc     = d;
    container_state(container)->pluginInstance = inst;
    container_set_editor(container, editor);
    [editor setFrame:NSMakeRect(0.0, 0.0, width, height)];
    [container addSubview:editor];
    return container;
}
