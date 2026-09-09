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

// HOW AN AUDIO UNIT HANDS A HOST ITS WINDOW. kAudioUnitProperty_CocoaUI names a bundle and a class
// inside it; the host loads the class, makes one, and asks it for a view. This is that class, and
// the view it hands back is the SAME one the VST3 editor shows - the plug-in's own createView().
//
// THE CLASS NAME COMES FROM THE BUILD, and that is not decoration. Objective-C class names are
// global to the PROCESS: two SynthLib-based Audio Units loaded into one host, each with a class
// called "SynthLibAUView", would collide - the runtime would keep one, say so in a log line nobody
// reads, and a host would then open one plug-in's editor from inside the other. do-plugin passes
// -DSYNTHLIB_AU_VIEW_CLASS with a name of the plug-in's own.

#import <Cocoa/Cocoa.h>
#import <AudioUnit/AUCocoaUIView.h>
#import <AudioUnit/AudioUnit.h>

#include "synthlibPlugin.h"
#include "synthlibPluginAu.h"

#ifndef SYNTHLIB_AU_VIEW_CLASS
#error "SYNTHLIB_AU_VIEW_CLASS must name this plug-in's own Cocoa view class - see the note above"
#endif

// Two levels, because a single-level paste or stringify would produce the MACRO's name rather than
// the name it expands to.
#define SL_PASTE2(a, b)     a ## b
#define SL_PASTE(a, b)      SL_PASTE2(a, b)
#define SL_STRINGIFY2(x)    #x
#define SL_STRINGIFY(x)     SL_STRINGIFY2(x)

#define SYNTHLIB_AU_CONTAINER_CLASS   SL_PASTE(SYNTHLIB_AU_VIEW_CLASS, Container)

static const tSynthLibPluginDesc * plugin_desc(void) {
    static const tSynthLibPluginDesc * d = NULL;

    if (d == NULL) {
        d = synthlib_plugin_descriptor();
    }
    return d;
}

CFStringRef synthlib_au_view_class_name(void) {
    static CFStringRef name = NULL;

    if (name == NULL) {
        name = CFStringCreateWithCString(NULL, SL_STRINGIFY(SYNTHLIB_AU_VIEW_CLASS),
                                         kCFStringEncodingUTF8);
    }
    return name;        // +0; the caller copies it if it wants to keep it
}

// ------------------------------------------------------------------------------------------------
// The container
// ------------------------------------------------------------------------------------------------

// WHY THERE IS A CONTAINER AT ALL. An Audio Unit host has no equivalent of VST3's
// checkSizeConstraint(): it resizes the view it was given and expects the view to cope. The canvas
// plug-in scales from WIDTH alone, so a taller-but-not-wider window would uncover rows rather than
// drawing larger - the very thing the application's own aspect lock prevents. Enforcing the ratio
// here gives the Audio Unit editor the same behaviour the VST3 one gets from the host.
@interface SYNTHLIB_AU_CONTAINER_CLASS : NSView
@property (assign, nonatomic) void * pluginInstance;
@property (strong, nonatomic) NSView * editorView;
@end

@implementation SYNTHLIB_AU_CONTAINER_CLASS

- (void)setFrameSize:(NSSize)newSize {
    const tSynthLibPluginDesc * d = plugin_desc();

    if (newSize.width < d->editorMinWidth) {
        newSize.width = d->editorMinWidth;
    }

    if (d->editorAspect > 0.0) {
        newSize.height = newSize.width / d->editorAspect;
    }
    [super setFrameSize:newSize];

    if (self.editorView != nil) {
        [self.editorView setFrame:NSMakeRect(0.0, 0.0, newSize.width, newSize.height)];

        if (d->cb.viewResized != NULL) {
            d->cb.viewResized(self.pluginInstance, (__bridge void *)self.editorView,
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
- (void)viewWillMoveToSuperview:(NSView *)newSuperview {
    const tSynthLibPluginDesc * d = plugin_desc();

    if ((newSuperview == nil) && (self.editorView != nil)) {
        if (d->cb.destroyView != NULL) {
            d->cb.destroyView(self.pluginInstance, (__bridge void *)self.editorView);
        }
        [self.editorView removeFromSuperview];
        self.editorView = nil;
    }
    [super viewWillMoveToSuperview:newSuperview];
}

@end

// ------------------------------------------------------------------------------------------------
// The factory class the host names
// ------------------------------------------------------------------------------------------------

@interface SYNTHLIB_AU_VIEW_CLASS : NSObject <AUCocoaUIBase>
@end

@implementation SYNTHLIB_AU_VIEW_CLASS

- (unsigned)interfaceVersion {
    return 0;
}

- (NSString *)description {
    return [NSString stringWithUTF8String:plugin_desc()->name];
}

- (NSView *)uiViewForAudioUnit:(AudioUnit)inAU withSize:(NSSize)inPreferredSize {
    const tSynthLibPluginDesc * d = plugin_desc();

    if (d->cb.createView == NULL) {
        return nil;
    }

    // BACK FROM AN AudioUnit TO OUR OWN INSTANCE. A Cocoa view factory is handed the AudioUnit and
    // nothing else, so the wrapper publishes the instance as a private property and this is the
    // only way across - see kSynthLibAuProperty_Instance.
    void *   inst = NULL;
    UInt32   size = (UInt32)sizeof(inst);
    OSStatus err  = AudioUnitGetProperty(inAU, kSynthLibAuProperty_Instance,
                                         kAudioUnitScope_Global, 0, &inst, &size);

    if ((err != noErr) || (inst == NULL)) {
        return nil;
    }
    // The host's preferred size is usually zero, meaning "whatever you like". The remembered width
    // wins over the default when there is one, exactly as it does on the VST3 side.
    double width = d->editorDefaultWidth;

    if (d->editorWidthLoad != NULL) {
        double saved = (double)d->editorWidthLoad();

        if (saved >= d->editorMinWidth) {
            width = saved;
        }
    }

    if (inPreferredSize.width >= d->editorMinWidth) {
        width = inPreferredSize.width;
    }
    double height = (d->editorAspect > 0.0) ? (width / d->editorAspect) : width;

    // createView() hands its view back RETAINED - see the note in synthlibPlugin.h - so the bridge
    // that takes ownership is __bridge_transfer, and ARC releases it with the container.
    NSView * editor = (__bridge_transfer NSView *)d->cb.createView(inst, width, height);

    if (editor == nil) {
        return nil;
    }
    SYNTHLIB_AU_CONTAINER_CLASS * container =
        [[SYNTHLIB_AU_CONTAINER_CLASS alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, height)];

    container.pluginInstance = inst;
    container.editorView     = editor;
    [editor setFrame:NSMakeRect(0.0, 0.0, width, height)];
    [container addSubview:editor];
    return container;
}

@end
