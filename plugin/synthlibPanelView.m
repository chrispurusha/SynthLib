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
// Notes: Docs/code-notes/synthlibPanelView.m.md - "// notes §k" refers there.

// notes §1

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "renderBackend.h"
#include "inputState.h"
#include "synthlibPanelView.h"
#include "synthlibRuntimeClass.h"


// A run-time class - see synthlibRuntimeClass.h. This declares the selectors of the two methods
// added to it that NSView doesn't have, and compiles to nothing.
@protocol SynthLibPanelViewMethods
- (void)tick:(NSTimer *)timer;
- (void)occlusionChanged:(NSNotification *)note;
@end

typedef struct {
    const tSynthLibPanel * panel;
    void *                 user;
    BOOL                   captured;   // press() grabbed a control; the pointer is hidden and held
    BOOL                   following;  // press() grabbed one that keeps the pointer visible
} tPanelState;

static Class gPanelViewClass = Nil;
static char  gTimerKey       = 0;

static tPanelState * panel_state(NSView * self) {
    return (tPanelState *)synthlib_runtime_class_state(gPanelViewClass, self);
}

static NSTimer * panel_timer(NSView * self) {
    return objc_getAssociatedObject(self, &gTimerKey);
}

static void panel_set_timer(NSView * self, NSTimer * timer) {
    objc_setAssociatedObject(self, &gTimerKey, timer, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
}

static void panel_redraw(NSView * self);
static void panel_sync(NSView * self);
static void panel_update_timer(NSView * self);

static uint32_t modifier_bits_from_ns(NSEventModifierFlags flags) {
    uint32_t bits = (uint32_t)eModifierNone;

    if ((flags & NSEventModifierFlagShift) != 0) {
        bits |= (uint32_t)eModifierShift;
    }

    if ((flags & NSEventModifierFlagCommand) != 0) {
        bits |= (uint32_t)eModifierCmd;
    }

    if ((flags & NSEventModifierFlagOption) != 0) {
        bits |= (uint32_t)eModifierAlt;
    }

    if ((flags & NSEventModifierFlagControl) != 0) {
        bits |= (uint32_t)eModifierCtrl;
    }
    return bits;
}

// THE POINTER, HIDDEN AND HELD WHILE A DIAL IS DRAGGED, as G2Alike's editor does it (plugin/g2View.m).
// Cocoa reports the pointer from the bottom left of the main screen and CGWarpMouseCursorPosition() wants
// the top left, hence the flip against the main screen's height. Process-wide: one drag at a time.
static BOOL    gCursorHidden  = NO;
static CGPoint gCursorRestore = {0.0, 0.0};

static void cursor_capture(void) {
    if (gCursorHidden) {
        return;
    }
    NSPoint   here  = [NSEvent mouseLocation];
    NSArray * all   = [NSScreen screens];
    CGFloat   mainH = ([all count] > 0) ? NSMaxY([[all objectAtIndex:0] frame]) : 0.0;

    gCursorRestore = CGPointMake(here.x, mainH - here.y);
    gCursorHidden  = YES;
    [NSCursor hide];
    CGAssociateMouseAndMouseCursorPosition(false);
}

static void cursor_release(void) {
    if (!gCursorHidden) {
        return;
    }
    gCursorHidden = NO;
    CGWarpMouseCursorPosition(gCursorRestore);
    CGAssociateMouseAndMouseCursorPosition(true);
    [NSCursor unhide];
}

// notes §3
static BOOL panel_should_repaint(NSView * self) {
    NSWindow * window = [self window];

    if ((window == nil) || [self isHiddenOrHasHiddenAncestor]) {
        return NO;
    }

    if ((panel_state(self)->panel != NULL) && (panel_state(self)->panel->paused != NULL) && panel_state(self)->panel->paused(panel_state(self)->user)) {
        return NO;    // the panel asked for no repainting - see tSynthLibPanel.paused
    }
    return ([window occlusionState] & NSWindowOcclusionStateVisible) != 0;
}

// notes §4
static void panel_update_timer(NSView * self) {
    BOOL wanted = panel_should_repaint(self);

    if (wanted && (panel_timer(self) == nil)) {
        // notes §5
        panel_set_timer(self, [NSTimer scheduledTimerWithTimeInterval:(1.0 / 30.0)
                               target:self
                               selector:@selector(tick:)
                               userInfo:nil
                               repeats:YES]);

        // Without this the timer stops while a menu is tracking or the window is being resized, and
        // the meters freeze at whatever they last showed - which reads as the plug-in having crashed.
        [[NSRunLoop currentRunLoop] addTimer:panel_timer(self) forMode:NSRunLoopCommonModes];

        // At once, rather than up to a thirtieth of a second later: coming back to an uncovered
        // window should not show a frame of whatever the meters read when it was covered.
        panel_redraw(self);
    } else if (!wanted && (panel_timer(self) != nil)) {
        [panel_timer(self) invalidate];    // the timer retains self, so this is also what lets the view go
        panel_set_timer(self, nil);
    }
}

// PER WINDOW, not once: the notification is observed against a specific window and a plug-in view is
// moved between them - re-parented as a host opens the editor in a floating window, docks it in a
// rack, or closes it. Registering against nil instead would catch every window in the host.
static void panel_viewDidMoveToWindow(NSView * self, SEL _cmd) {
    SYNTHLIB_SUPER_VOID(self, _cmd);

    [[NSNotificationCenter defaultCenter] removeObserver:self
     name:NSWindowDidChangeOcclusionStateNotification
     object:nil];

    if ([self window] != nil) {
        [[NSNotificationCenter defaultCenter] addObserver:self
         selector:@selector(occlusionChanged:)
         name:NSWindowDidChangeOcclusionStateNotification
         object:[self window]];
    }
    panel_update_timer(self);
}

static void panel_occlusionChanged(NSView * self, SEL _cmd, NSNotification * note) {
    (void)_cmd;
    (void)note;
    panel_update_timer(self);
}

static void panel_viewDidHide(NSView * self, SEL _cmd) {
    SYNTHLIB_SUPER_VOID(self, _cmd);
    panel_update_timer(self);
}

static void panel_viewDidUnhide(NSView * self, SEL _cmd) {
    SYNTHLIB_SUPER_VOID(self, _cmd);
    panel_update_timer(self);
}

static BOOL panel_isOpaque(NSView * self, SEL _cmd) {
    (void)self;
    (void)_cmd;
    return YES;
}

static BOOL panel_acceptsFirstResponder(NSView * self, SEL _cmd) {
    (void)self;
    (void)_cmd;
    return YES;
}

// A host click that lands on the plug-in's window should reach the control it hit, rather than
// being swallowed as the click that merely focuses the window.
static BOOL panel_acceptsFirstMouse(NSView * self, SEL _cmd, NSEvent * event) {
    (void)self;
    (void)_cmd;
    (void)event;
    return YES;
}

// notes §6
static void panel_setFrameSize(NSView * self, SEL _cmd, NSSize newSize) {
    SYNTHLIB_SUPER_ARG(self, _cmd, NSSize, newSize);
    panel_redraw(self);
}

// One more once the drag stops, because the tick stood down for the duration and the last
// -setFrameSize: may have arrived mid-frame.
static void panel_viewDidEndLiveResize(NSView * self, SEL _cmd) {
    SYNTHLIB_SUPER_VOID(self, _cmd);
    panel_redraw(self);
}

static void panel_tick(NSView * self, SEL _cmd, NSTimer * timer) {
    (void)_cmd;
    (void)timer;

    // notes §7
    if ([self inLiveResize]) {
        return;
    }
    panel_redraw(self);
}

// This editor's instance into the draw layer - see tSynthLibPanel.sync.
static void panel_sync(NSView * self) {
    if ((panel_state(self)->panel != NULL) && (panel_state(self)->panel->sync != NULL)) {
        panel_state(self)->panel->sync(panel_state(self)->user);
    }
}

static void panel_redraw(NSView * self) {
    NSRect backing = [self convertRectToBacking:[self bounds]];

    // A view with no window has no drawable behind it, and a zero-sized one would ask the backend for
    // render targets it cannot make. Both are reachable: -redraw is called from -mouseDown: and from
    // -updateTimer as well as from the tick.
    if (  ([self window] == nil) || (backing.size.width < 1.0) || (backing.size.height < 1.0)
       || (panel_state(self)->panel == NULL) || (panel_state(self)->panel->frame == NULL)) {
        return;
    }
    // notes §8
    gfx_attach_window((__bridge void *)self);

    panel_sync(self);

    // notes §9
    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    panel_state(self)->panel->frame(panel_state(self)->user, (int)backing.size.width, (int)backing.size.height);
    gfx_present();

    [CATransaction commit];
}

// AppKit's origin is bottom left and the canvas's is top left, so y is flipped here rather than in
// the drawing code - the renderer's coordinate space is shared with the applications and must not be
// bent to suit one host view.
static NSPoint panel_canvas_point(NSView * self, NSEvent * event) {
    NSPoint local = [self convertPoint:[event locationInWindow] fromView:nil];
    double  width = ((panel_state(self)->panel != NULL) && (panel_state(self)->panel->canvasWidth > 0.0)) ? panel_state(self)->panel->canvasWidth : 1.0;
    double  scale = [self bounds].size.width / width;

    if (scale <= 0.0) {
        scale = 1.0;
    }
    return NSMakePoint(local.x / scale, ([self bounds].size.height - local.y) / scale);
}

static void panel_flagsChanged(NSView * self, SEL _cmd, NSEvent * event) {
    set_modifier_state(modifier_bits_from_ns([event modifierFlags]));
    SYNTHLIB_SUPER_ARG(self, _cmd, NSEvent *, event);
}

// GLFW's codes for the editing keys, which is what SynthLib's popups read; written out so this file needs no
// GLFW header. Every other key is passed as the character it typed.
static int glfw_key_for_mac(unsigned short keyCode) {
    switch (keyCode) {
        case 53:  return 256;   // Escape

        case 36:  return 257;   // Return

        case 48:  return 258;   // Tab

        case 51:  return 259;   // Backspace

        case 117: return 261;   // Delete

        case 124: return 262;   // Right

        case 123: return 263;   // Left

        case 125: return 264;   // Down

        case 126: return 265;   // Up

        case 116: return 266;   // Page Up

        case 121: return 267;   // Page Down

        case 115: return 268;   // Home

        case 119: return 269;   // End

        case 76:  return 335;   // keypad Enter

        default:  return -1;
    }
}

static void panel_keyDown(NSView * self, SEL _cmd, NSEvent * event) {
    if ((panel_state(self)->panel == NULL) || (panel_state(self)->panel->key == NULL)) {
        SYNTHLIB_SUPER_ARG(self, _cmd, NSEvent *, event);
        return;
    }
    NSString *   chars     = [event characters];
    int          key       = glfw_key_for_mac([event keyCode]);
    unsigned int codepoint = ((key == -1) && ([chars length] > 0)) ? (unsigned int)[chars characterAtIndex:0] : 0u;

    set_modifier_state(modifier_bits_from_ns([event modifierFlags]));
    panel_sync(self);

    if (panel_state(self)->panel->key(panel_state(self)->user, key, codepoint, [event isARepeat] == YES)) {
        panel_redraw(self);
        return;
    }
    SYNTHLIB_SUPER_ARG(self, _cmd, NSEvent *, event);
}

// Points to the canvas's logical units, for a movement rather than a position.
static double panel_logical_per_point(NSView * self) {
    double width = ((panel_state(self)->panel != NULL) && (panel_state(self)->panel->canvasWidth > 0.0)) ? panel_state(self)->panel->canvasWidth : 1.0;
    double scale = [self bounds].size.width / width;

    return (scale > 0.0) ? (1.0 / scale) : 1.0;
}

static void panel_mouseDown(NSView * self, SEL _cmd, NSEvent * event) {
    (void)_cmd;
    NSPoint p = panel_canvas_point(self, event);

    if (panel_state(self)->panel == NULL) {
        return;
    }
    set_modifier_state(modifier_bits_from_ns([event modifierFlags]));

    // notes §10
    panel_sync(self);

    // the keyboard is claimed by a click IN the view, not when it opens, so a host keeps its own keys until then
    if ((panel_state(self)->panel->key != NULL) && ([self window] != nil)) {
        [[self window] makeFirstResponder:self];
    }

    if (panel_state(self)->panel->pointer != NULL) {
        panel_state(self)->panel->pointer(p.x, p.y);   // a click is a position too, and a trackpad tap sends no move
    }

    if (panel_state(self)->panel->press != NULL) {
        BOOL grabbed = panel_state(self)->panel->press(panel_state(self)->user, p.x, p.y) ? YES : NO;
        BOOL keeps   = grabbed && (panel_state(self)->panel->keepsPointer != NULL) && (panel_state(self)->panel->dragTo != NULL)
                       && panel_state(self)->panel->keepsPointer(panel_state(self)->user);

        panel_state(self)->following = keeps;
        panel_state(self)->captured  = grabbed && !keeps;

        if (panel_state(self)->captured) {
            cursor_capture();
        }
    } else if (panel_state(self)->panel->click != NULL) {
        (void)panel_state(self)->panel->click(panel_state(self)->user, p.x, p.y);
    }
    panel_redraw(self);
    panel_update_timer(self);   // a click may have paused or resumed the panel
}

static void panel_mouseUp(NSView * self, SEL _cmd, NSEvent * event) {
    (void)_cmd;
    NSPoint p = panel_canvas_point(self, event);

    if ((panel_state(self)->panel == NULL) || (panel_state(self)->panel->press == NULL)) {
        return;
    }
    set_modifier_state(modifier_bits_from_ns([event modifierFlags]));

    panel_state(self)->following = NO;

    if (panel_state(self)->captured) {
        panel_state(self)->captured = NO;
        cursor_release();
    }

    if (panel_state(self)->panel->release != NULL) {
        panel_sync(self);
        panel_state(self)->panel->release(panel_state(self)->user, p.x, p.y);
    }
    panel_redraw(self);
}

// notes §11
static void panel_updateTrackingAreas(NSView * self, SEL _cmd) {
    SYNTHLIB_SUPER_VOID(self, _cmd);

    for (NSTrackingArea * area in [self trackingAreas]) {
        [self removeTrackingArea:area];
    }

    NSTrackingArea * area =
        [[NSTrackingArea alloc] initWithRect:[self bounds]
         options:(NSTrackingMouseMoved | NSTrackingActiveInActiveApp |
                  NSTrackingInVisibleRect)
         owner:self
         userInfo:nil];

    [self addTrackingArea:area];
}

static void panel_mouseMoved(NSView * self, SEL _cmd, NSEvent * event) {
    (void)_cmd;
    NSPoint p = panel_canvas_point(self, event);

    if ((panel_state(self)->panel == NULL) || (panel_state(self)->panel->pointer == NULL)) {
        return;
    }
    panel_state(self)->panel->pointer(p.x, p.y);

    // ONLY WHILE A MENU IS OPEN. Nothing else on these panels responds to a bare mouse move, and
    // repainting on every one would put a 60-plus Hz redraw under the host's cursor for no visible
    // change. The 30 Hz timer covers everything else.
    if ((panel_state(self)->panel->menuActive != NULL) && panel_state(self)->panel->menuActive()) {
        panel_redraw(self);
    }
}

// A drag is a stream of clicks: only continuous controls (GenBridge's trim) respond to one, and
// routing it through the same hit test keeps that decision in one place.
static void panel_mouseDragged(NSView * self, SEL _cmd, NSEvent * event) {
    (void)_cmd;

    if ((panel_state(self)->panel != NULL) && (panel_state(self)->panel->press != NULL)) {
        set_modifier_state(modifier_bits_from_ns([event modifierFlags]));

        if (panel_state(self)->following) {
            NSPoint p = panel_canvas_point(self, event);

            panel_sync(self);
            panel_state(self)->panel->dragTo(panel_state(self)->user, p.x, p.y);
            panel_redraw(self);
        } else if (panel_state(self)->captured && (panel_state(self)->panel->dragBy != NULL)) {
            double k = panel_logical_per_point(self);

            panel_sync(self);
            panel_state(self)->panel->dragBy(panel_state(self)->user, [event deltaX] * k, [event deltaY] * k);
            panel_redraw(self);
        }
        return;
    }
    [self mouseDown:event];
}

static void panel_scrollWheel(NSView * self, SEL _cmd, NSEvent * event) {
    NSPoint p  = panel_canvas_point(self, event);

    if ((panel_state(self)->panel == NULL) || (panel_state(self)->panel->scroll == NULL)) {
        SYNTHLIB_SUPER_ARG(self, _cmd, NSEvent *, event);   // let the host scroll whatever it is scrolling
        return;
    }
    // Lines from a wheel, points from a trackpad; both into logical units, downward positive.
    double  dy = -[event scrollingDeltaY] * ([event hasPreciseScrollingDeltas] ? panel_logical_per_point(self) : 1.0);

    panel_sync(self);
    panel_state(self)->panel->scroll(panel_state(self)->user, p.x, p.y, dy);
    panel_redraw(self);
}

static void panel_removeFromSuperview(NSView * self, SEL _cmd) {
    if (panel_state(self)->captured) {          // a host closing the editor mid-drag must not leave the pointer hidden
        panel_state(self)->captured = NO;
        cursor_release();
    }
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [panel_timer(self) invalidate];        // the timer retains self; leaving it running leaks the view
    panel_set_timer(self, nil);

    // Hand back the layer and render targets. Without this a host that opens and closes editors
    // would exhaust the backend's window slots, since every new view is a different pointer.
    gfx_detach_window((__bridge void *)self);

    SYNTHLIB_SUPER_VOID(self, _cmd);
}

// RETAINED (+1), as SynthLib's createView() contract requires: the wrapper owns the view from here and
// releases it after taking it out of the window - -removeFromSuperview is where it lets go of its
// timer and its drawing surface.
static void panel_make_class(void) {
    Class cls = synthlib_runtime_class_begin([NSView class], "SynthLibPanelView", sizeof(tPanelState),
                                             (const void *)&gPanelViewClass);

    synthlib_runtime_class_override(cls, @selector(setFrameSize:), panel_setFrameSize);
    synthlib_runtime_class_override(cls, @selector(viewDidMoveToWindow), panel_viewDidMoveToWindow);
    synthlib_runtime_class_override(cls, @selector(viewDidHide), panel_viewDidHide);
    synthlib_runtime_class_override(cls, @selector(viewDidUnhide), panel_viewDidUnhide);
    synthlib_runtime_class_override(cls, @selector(viewDidEndLiveResize), panel_viewDidEndLiveResize);
    synthlib_runtime_class_override(cls, @selector(isOpaque), panel_isOpaque);
    synthlib_runtime_class_override(cls, @selector(acceptsFirstResponder), panel_acceptsFirstResponder);
    synthlib_runtime_class_override(cls, @selector(acceptsFirstMouse:), panel_acceptsFirstMouse);
    synthlib_runtime_class_override(cls, @selector(flagsChanged:), panel_flagsChanged);
    synthlib_runtime_class_override(cls, @selector(keyDown:), panel_keyDown);
    synthlib_runtime_class_override(cls, @selector(mouseDown:), panel_mouseDown);
    synthlib_runtime_class_override(cls, @selector(mouseUp:), panel_mouseUp);
    synthlib_runtime_class_override(cls, @selector(mouseMoved:), panel_mouseMoved);
    synthlib_runtime_class_override(cls, @selector(mouseDragged:), panel_mouseDragged);
    synthlib_runtime_class_override(cls, @selector(scrollWheel:), panel_scrollWheel);
    synthlib_runtime_class_override(cls, @selector(updateTrackingAreas), panel_updateTrackingAreas);
    synthlib_runtime_class_override(cls, @selector(removeFromSuperview), panel_removeFromSuperview);

    // The two NSView doesn't have: what the timer and the notification call.
    class_addMethod(cls, @selector(tick:), (IMP)panel_tick, "v@:@");
    class_addMethod(cls, @selector(occlusionChanged:), (IMP)panel_occlusionChanged, "v@:@");

    objc_registerClassPair(cls);
    gPanelViewClass = cls;
}

void * synthlib_panel_view_create(const tSynthLibPanel * panel, void * user, double width, double height) {
    static dispatch_once_t once = 0;
    NSView *               view = nil;

    dispatch_once(&once, ^{
        panel_make_class();
    });
    view                     = [[gPanelViewClass alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, height)];
    panel_state(view)->panel = panel;
    panel_state(view)->user  = user;

    // The application chooses its backend from a saved setting at start-up; a plug-in has no such
    // setting and no window of its own, so the choice is asserted here before anything touches it.
    gfx_backend_choose(eRenderBackendMetal);

    // notes §2
    gfx_attach_window((__bridge void *)view);

    // NO TIMER YET. There is no window at this point, so there is nothing to repaint for;
    // viewDidMoveToWindow starts one once there is. See panel_update_timer().

    // AFTER the surface exists, as the per-project views did.
    if ((panel != NULL) && (panel->init != NULL)) {
        panel->init();
    }
    return (__bridge_retained void *)view;
}
