/*
 * SynthLib - shared code for the synth editors and their plug-ins.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program. If
 * not, see <https://www.gnu.org/licenses/>.
 */
// Notes: Docs/code-notes/synthlibResizeGrip.m.md - "// notes §k" refers there.

#include <math.h>

#include "synthlibResizeGrip.h"
#include "synthlibRuntimeClass.h"

#define GRIP_SIZE    (14.0)

typedef struct {
    const tSynthLibPluginDesc * desc;
    void *                      inst;
    NSPoint                     startMouse;   // screen points
    NSSize                      startSize;
} tGripState;

static Class gGripClass = Nil;

static NSCursor * grip_cursor(void);

static tGripState * grip_state(NSView * self) {
    return (tGripState *)synthlib_runtime_class_state(gGripClass, self);
}

// notes §2: the editor's limits - the width to its range, the height to the aspect, or with editorFreeAspect to at
// least the minimum width's height
static NSSize constrained(const tSynthLibPluginDesc * d, NSSize s) {
    double minHeight = (d->editorAspect > 0.0) ? (d->editorMinWidth / d->editorAspect) : d->editorMinWidth;

    s.width = fmax(s.width, d->editorMinWidth);

    if (d->editorMaxWidth > 0.0) {
        s.width = fmin(s.width, d->editorMaxWidth);
    }

    if (d->editorFreeAspect || (d->editorAspect <= 0.0)) {
        s.height = fmax(s.height, minHeight);
    } else {
        s.height = s.width / d->editorAspect;
    }
    return s;
}

static void grip_drawRect(NSView * self, SEL _cmd, NSRect dirty) {
    (void)_cmd;
    (void)dirty;
    NSRect       b    = [self bounds];
    NSBezierPath * p  = [NSBezierPath bezierPath];
    bool         flip = [self isFlipped];

    for (int i = 1; i <= 3; i++) {   // three diagonals, the usual corner grip
        double k = (double)i * (GRIP_SIZE / 4.0);
        double y = flip ? (b.size.height - 1.0) : 1.0;

        [p moveToPoint:NSMakePoint(b.size.width - k, y)];
        [p lineToPoint:NSMakePoint(b.size.width - 1.0, flip ? (y - k + 1.0) : (y + k - 1.0))];
    }

    [[NSColor colorWithWhite:0.55 alpha:0.9] setStroke];
    [p setLineWidth:1.0];
    [p stroke];
}

static void grip_mouseDown(NSView * self, SEL _cmd, NSEvent * event) {
    (void)_cmd;
    (void)event;
    tGripState * g = grip_state(self);

    g->startMouse = [NSEvent mouseLocation];
    g->startSize  = [[self superview] frame].size;
}

// notes §3: the size the drag asks for, from where the pointer has gone since the press
static void grip_mouseDragged(NSView * self, SEL _cmd, NSEvent * event) {
    (void)_cmd;
    (void)event;
    tGripState * g    = grip_state(self);
    NSPoint      now  = [NSEvent mouseLocation];
    NSSize       want = NSMakeSize(g->startSize.width + (now.x - g->startMouse.x), g->startSize.height - (now.y - g->startMouse.y));

    [grip_cursor() set];
    want = constrained(g->desc, want);
    (void)synthlib_plugin_request_resize(g->inst, round(want.width), round(want.height));
}

// notes §4: before macOS 15 there is no public diagonal resize cursor
static NSCursor * grip_cursor(void) {
    if (@available(macOS 15.0, *)) {
        return [NSCursor frameResizeCursorFromPosition:NSCursorFrameResizePositionBottomRight inDirections:NSCursorFrameResizeDirectionsAll];
    }
    return [NSCursor crosshairCursor];
}

static void grip_resetCursorRects(NSView * self, SEL _cmd) {
    (void)_cmd;
    [self addCursorRect:[self bounds] cursor:grip_cursor()];
}

// notes §5: a tracking area that works when the window is not key - a plug-in window often is not - sets the cursor
// itself on the way in and back to the arrow on the way out, and keeps it while the pointer moves over the grip
static void grip_updateTrackingAreas(NSView * self, SEL _cmd) {
    for (NSTrackingArea * area in [[self trackingAreas] copy]) {
        [self removeTrackingArea:area];
    }

    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                       options:(NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingCursorUpdate |
                                                                NSTrackingActiveAlways | NSTrackingInVisibleRect)
                                                         owner:self userInfo:nil]];
    SYNTHLIB_SUPER_VOID(self, _cmd);
}

static void grip_set_cursor(NSView * self, SEL _cmd, NSEvent * event) {
    (void)self;
    (void)_cmd;
    (void)event;
    [grip_cursor() set];
}

static void grip_mouseExited(NSView * self, SEL _cmd, NSEvent * event) {
    (void)self;
    (void)_cmd;
    (void)event;
    [[NSCursor arrowCursor] set];
}

static BOOL grip_acceptsFirstMouse(NSView * self, SEL _cmd, NSEvent * event) {
    (void)self;
    (void)_cmd;
    (void)event;
    return YES;
}

static void grip_make_class(void) {
    Class cls = synthlib_runtime_class_begin([NSView class], "SynthLibResizeGrip", sizeof(tGripState), (const void *)&gGripClass);

    synthlib_runtime_class_override(cls, @selector(drawRect:), grip_drawRect);
    synthlib_runtime_class_override(cls, @selector(mouseDown:), grip_mouseDown);
    synthlib_runtime_class_override(cls, @selector(mouseDragged:), grip_mouseDragged);
    synthlib_runtime_class_override(cls, @selector(resetCursorRects), grip_resetCursorRects);
    synthlib_runtime_class_override(cls, @selector(updateTrackingAreas), grip_updateTrackingAreas);
    synthlib_runtime_class_override(cls, @selector(mouseEntered:), grip_set_cursor);
    synthlib_runtime_class_override(cls, @selector(mouseMoved:), grip_set_cursor);
    synthlib_runtime_class_override(cls, @selector(cursorUpdate:), grip_set_cursor);
    synthlib_runtime_class_override(cls, @selector(mouseExited:), grip_mouseExited);
    synthlib_runtime_class_override(cls, @selector(acceptsFirstMouse:), grip_acceptsFirstMouse);
    objc_registerClassPair(cls);
    gGripClass = cls;
}

NSView * synthlib_resize_grip_attach(NSView * parent, const tSynthLibPluginDesc * desc, void * inst) {
    static dispatch_once_t once = 0;

    if ((parent == nil) || (desc == NULL) || (desc->editorMinWidth >= desc->editorDefaultWidth)) {
        return nil;   // a fixed-size editor has nothing to grip
    }
    dispatch_once(&once, ^{
        grip_make_class();
    });
    NSRect   b    = [parent bounds];
    bool     flip = [parent isFlipped];
    NSView * grip = [[gGripClass alloc] initWithFrame:NSMakeRect(b.size.width - GRIP_SIZE, flip ? (b.size.height - GRIP_SIZE) : 0.0,
                                                                 GRIP_SIZE, GRIP_SIZE)];

    *grip_state(grip)     = (tGripState){.desc = desc, .inst = inst};
    grip.autoresizingMask = NSViewMinXMargin | (flip ? NSViewMinYMargin : NSViewMaxYMargin);   // stays in the corner
    [parent addSubview:grip positioned:NSWindowAbove relativeTo:nil];
    return grip;
}
