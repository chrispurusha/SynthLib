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

// See synthlibPanelView.h. This was GenBridge's gbView.m, which MidiSyncTool's msView.m had been
// copied from; the notes below are the ones both carried, and each records something measured or
// something that went wrong in a real host.
//
// METAL, so a PLAIN NSView. There is no context for the view to own - it is layer-hosting and the
// CAMetalLayer is the surface - which is why there is no NSOpenGLView and no -prepareOpenGL.

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "renderBackend.h"
#include "synthlibPanelView.h"

#ifndef SYNTHLIB_PANEL_VIEW_CLASS
#error "SYNTHLIB_PANEL_VIEW_CLASS must name this plug-in's own panel view class - see synthlibPanelView.h"
#endif

@interface SYNTHLIB_PANEL_VIEW_CLASS : NSView
@property (nonatomic, assign) const tSynthLibPanel * panel;
@property (nonatomic, assign) void *                 user;
@property (nonatomic, strong) NSTimer *              timer;
@end

@implementation SYNTHLIB_PANEL_VIEW_CLASS

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];

    if (self == nil) {
        return nil;
    }

    // The application chooses its backend from a saved setting at start-up; a plug-in has no such
    // setting and no window of its own, so the choice is asserted here before anything touches it.
    gfx_backend_choose(eRenderBackendMetal);

    // LAYER-HOSTING, AND THE ORDER MATTERS: gfx_attach_window() assigns the layer and only then is
    // wantsLayer set, which is what tells AppKit the contents belong to the layer and that it must
    // not draw over them. It is handed the VIEW rather than a window - in a plug-in the window
    // belongs to the host, and we may never see it.
    gfx_attach_window((__bridge void *)self);

    // NO TIMER YET. There is no window at this point, so there is nothing to repaint for;
    // -viewDidMoveToWindow starts one once there is. See -updateTimer.
    return self;
}

// Whether a repaint would be seen by anyone. THE EDITOR BEING CLOSED IS NOT THE ONLY WAY TO STOP
// SHOWING IT: the host calls removed() for that and the timer goes with the view, but a window that
// is minimised, completely covered by another, or on an inactive Space is just as invisible and the
// view is still in the hierarchy. So is one in a host that HIDES its plug-in view rather than
// removing it, which some do when switching between panels in a rack. In every one of those cases
// this used to go on drawing thirty full Metal frames a second - a frame has no dirty check - into a
// surface nobody was looking at.
- (BOOL)shouldRepaint {
    NSWindow * window = [self window];

    if ((window == nil) || [self isHiddenOrHasHiddenAncestor]) {
        return NO;
    }

    return ([window occlusionState] & NSWindowOcclusionStateVisible) != 0;
}

// The timer exists exactly while it is worth having. Starting one is cheap, so this is driven from
// the notifications rather than by letting a tick fire and return early: a tick that returns early
// still wakes the process thirty times a second, which is most of what there was to save on a
// machine that has gone to sleep with a project open.
- (void)updateTimer {
    BOOL wanted = [self shouldRepaint];

    if (wanted && (self.timer == nil)) {
        // A timer rather than a CVDisplayLink. The panels show meters and live telemetry, so they
        // repaint continuously rather than on demand, but nothing here is worth a display link's
        // complications - and a link fires on its own thread, which would mean marshalling every
        // frame back to the main one before touching AppKit.
        self.timer = [NSTimer scheduledTimerWithTimeInterval:(1.0 / 30.0)
                                                      target:self
                                                    selector:@selector(tick:)
                                                    userInfo:nil
                                                     repeats:YES];

        // Without this the timer stops while a menu is tracking or the window is being resized, and
        // the meters freeze at whatever they last showed - which reads as the plug-in having crashed.
        [[NSRunLoop currentRunLoop] addTimer:self.timer forMode:NSRunLoopCommonModes];

        // At once, rather than up to a thirtieth of a second later: coming back to an uncovered
        // window should not show a frame of whatever the meters read when it was covered.
        [self redraw];
    } else if (!wanted && (self.timer != nil)) {
        [self.timer invalidate];    // the timer retains self, so this is also what lets the view go
        self.timer = nil;
    }
}

// PER WINDOW, not once: the notification is observed against a specific window and a plug-in view is
// moved between them - re-parented as a host opens the editor in a floating window, docks it in a
// rack, or closes it. Registering against nil instead would catch every window in the host.
- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];

    [[NSNotificationCenter defaultCenter] removeObserver:self
                                                    name:NSWindowDidChangeOcclusionStateNotification
                                                  object:nil];

    if ([self window] != nil) {
        [[NSNotificationCenter defaultCenter] addObserver:self
                                                 selector:@selector(occlusionChanged:)
                                                     name:NSWindowDidChangeOcclusionStateNotification
                                                   object:[self window]];
    }

    [self updateTimer];
}

- (void)occlusionChanged:(NSNotification *)note {
    (void)note;
    [self updateTimer];
}

- (void)viewDidHide {
    [super viewDidHide];
    [self updateTimer];
}

- (void)viewDidUnhide {
    [super viewDidUnhide];
    [self updateTimer];
}

- (BOOL)isOpaque {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

// A host click that lands on the plug-in's window should reach the control it hit, rather than
// being swallowed as the click that merely focuses the window.
- (BOOL)acceptsFirstMouse:(NSEvent *)event {
    (void)event;
    return YES;
}

// IN STEP WITH THE RESIZE. The 30 Hz timer is fine for meters and hopeless for a drag: the host moves
// the frame at display rate, so the content arrived up to a thirtieth of a second behind the window
// edge and visibly lagged it. The backend reallocates its render targets from the new size on the
// next frame, so there is nothing else to tell.
- (void)setFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    [self redraw];
}

// One more once the drag stops, because the tick stood down for the duration and the last
// -setFrameSize: may have arrived mid-frame.
- (void)viewDidEndLiveResize {
    [super viewDidEndLiveResize];
    [self redraw];
}

- (void)tick:(NSTimer *)timer {
    (void)timer;

    // NOT WHILE THE USER IS DRAGGING THE EDGE. -setFrameSize: is already redrawing, at least as often
    // as the timer would and usually more, so a tick here draws a second frame nobody asked for - and
    // that is not merely wasted, it is actively what made a resize judder.
    //
    // MEASURED: a redraw takes a drawable from the layer and presents it, and CAMetalLayer keeps a
    // small pool. Present more often than the display refreshes and -nextDrawable blocks until one
    // comes free - up to a full refresh, 15.4 ms of the 17 ms a resize step was costing. That block
    // happens INSIDE AppKit's drag loop, so the window itself stops moving while we wait for a frame
    // the user was never going to see. Reallocating the render targets measured 0.00 ms.
    if ([self inLiveResize]) {
        return;
    }
    [self redraw];
}

// This editor's instance into the draw layer - see tSynthLibPanel.sync.
- (void)sync {
    if ((self.panel != NULL) && (self.panel->sync != NULL)) {
        self.panel->sync(self.user);
    }
}

- (void)redraw {
    NSRect backing = [self convertRectToBacking:[self bounds]];

    // A view with no window has no drawable behind it, and a zero-sized one would ask the backend for
    // render targets it cannot make. Both are reachable: -redraw is called from -mouseDown: and from
    // -updateTimer as well as from the tick.
    if (([self window] == nil) || (backing.size.width < 1.0) || (backing.size.height < 1.0)
       || (self.panel == NULL) || (self.panel->frame == NULL)) {
        return;
    }

    // SELECT THIS VIEW'S CONTEXT FIRST. With two editors open, whichever drew last left the backend
    // pointing at its own layer; drawing without claiming ours would paint into the other one's
    // window. Attaching an already-known view is a pointer assignment, so this is cheap enough to do
    // every frame and removes any need to track whose turn it is.
    gfx_attach_window((__bridge void *)self);

    [self sync];

    // ONE FRAME, ONE TRANSACTION. The frame moves the layer's geometry and gfx_present() hands over
    // the pixels for it; as two separate Core Animation transactions the layer had its NEW size and
    // its OLD contents, stretched to fit, for one commit - seen as a jump on every step of a resize.
    // The backend presents with presentsWithTransaction set for a hosted view, which is what makes
    // putting them in the same transaction meaningful.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];

    self.panel->frame(self.user, (int)backing.size.width, (int)backing.size.height);
    gfx_present();

    [CATransaction commit];
}

// AppKit's origin is bottom left and the canvas's is top left, so y is flipped here rather than in
// the drawing code - the renderer's coordinate space is shared with the applications and must not be
// bent to suit one host view.
- (NSPoint)canvasPointFor:(NSEvent *)event {
    NSPoint local = [self convertPoint:[event locationInWindow] fromView:nil];
    double  width = ((self.panel != NULL) && (self.panel->canvasWidth > 0.0)) ? self.panel->canvasWidth : 1.0;
    double  scale = [self bounds].size.width / width;

    if (scale <= 0.0) {
        scale = 1.0;
    }
    return NSMakePoint(local.x / scale, ([self bounds].size.height - local.y) / scale);
}

- (void)mouseDown:(NSEvent *)event {
    NSPoint p = [self canvasPointFor:event];

    if (self.panel == NULL) {
        return;
    }

    // SYNCED BEFORE THE HIT TEST, not just before the draw. The draw layer's notion of which editor
    // it is serving is file-scope, and the hit test consults it: GenBridge only offers its Measure
    // and Offset controls when it believes it is drawing the instrument, and with an effect and an
    // instrument both open the effect's repaint had already told it otherwise by the time a click
    // reached the instrument - so those two controls silently did nothing while every other one
    // worked. The values go with it, for the same reason: a stepper steps from what the layer holds.
    [self sync];

    if (self.panel->pointer != NULL) {
        self.panel->pointer(p.x, p.y);   // a click is a position too, and a trackpad tap sends no move
    }

    if (self.panel->click != NULL) {
        (void)self.panel->click(self.user, p.x, p.y);
    }
    [self redraw];
}

// THE POINTER POSITION, for an open drop-down to highlight under.
//
// NSTrackingInVisibleRect means AppKit maintains the region itself as the view is resized, so this
// does not have to be torn down and rebuilt on every geometry change - which matters here, where the
// host owns the window and resizing is already the fiddliest part of this view.
- (void)updateTrackingAreas {
    [super updateTrackingAreas];

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

- (void)mouseMoved:(NSEvent *)event {
    NSPoint p = [self canvasPointFor:event];

    if ((self.panel == NULL) || (self.panel->pointer == NULL)) {
        return;
    }
    self.panel->pointer(p.x, p.y);

    // ONLY WHILE A MENU IS OPEN. Nothing else on these panels responds to a bare mouse move, and
    // repainting on every one would put a 60-plus Hz redraw under the host's cursor for no visible
    // change. The 30 Hz timer covers everything else.
    if ((self.panel->menuActive != NULL) && self.panel->menuActive()) {
        [self redraw];
    }
}

// A drag is a stream of clicks: only continuous controls (GenBridge's trim) respond to one, and
// routing it through the same hit test keeps that decision in one place.
- (void)mouseDragged:(NSEvent *)event {
    [self mouseDown:event];
}

- (void)removeFromSuperview {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [self.timer invalidate];        // the timer retains self; leaving it running leaks the view
    self.timer = nil;

    // Hand back the layer and render targets. Without this a host that opens and closes editors
    // would exhaust the backend's window slots, since every new view is a different pointer.
    gfx_detach_window((__bridge void *)self);

    [super removeFromSuperview];
}

@end

// RETAINED (+1), as SynthLib's createView() contract requires: the wrapper owns the view from here and
// releases it after taking it out of the window - -removeFromSuperview is where it lets go of its
// timer and its drawing surface.
void * synthlib_panel_view_create(const tSynthLibPanel * panel, void * user, double width, double height) {
    SYNTHLIB_PANEL_VIEW_CLASS * view = [[SYNTHLIB_PANEL_VIEW_CLASS alloc] initWithFrame:NSMakeRect(0.0, 0.0, width, height)];

    view.panel = panel;
    view.user  = user;

    // AFTER the surface exists (-initWithFrame: attached it), as the per-project views did.
    if ((panel != NULL) && (panel->init != NULL)) {
        panel->init();
    }
    return (__bridge_retained void *)view;
}
