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

    // notes §2
    gfx_attach_window((__bridge void *)self);

    // NO TIMER YET. There is no window at this point, so there is nothing to repaint for;
    // -viewDidMoveToWindow starts one once there is. See -updateTimer.
    return self;
}

// notes §3
- (BOOL)shouldRepaint {
    NSWindow * window = [self window];

    if ((window == nil) || [self isHiddenOrHasHiddenAncestor]) {
        return NO;
    }

    return ([window occlusionState] & NSWindowOcclusionStateVisible) != 0;
}

// notes §4
- (void)updateTimer {
    BOOL wanted = [self shouldRepaint];

    if (wanted && (self.timer == nil)) {
        // notes §5
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

// notes §6
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

    // notes §7
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

    // notes §8
    gfx_attach_window((__bridge void *)self);

    [self sync];

    // notes §9
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

    // notes §10
    [self sync];

    if (self.panel->pointer != NULL) {
        self.panel->pointer(p.x, p.y);   // a click is a position too, and a trackpad tap sends no move
    }

    if (self.panel->click != NULL) {
        (void)self.panel->click(self.user, p.x, p.y);
    }
    [self redraw];
}

// notes §11
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
