# synthlibResizeGrip.m

## 1. Why a grip in the editor
Some hosts give a plug-in window no corner to drag: Ableton Live resizes a VST3's window but not an Audio Unit's,
whatever the view says (2026-11-16, found when G2Alike's AU would not resize there). So every resizable SynthLib
editor carries its own grip, three diagonals in its bottom-right corner, above the editor, staying in the corner as it
resizes. Dragging it asks the wrapper for the size (`synthlib_plugin_request_resize()`): the VST3 asks the host
(IPlugFrame::resizeView), the AU resizes its own container and the host's window follows the view, as hosts do. A
fixed-size editor (minimum width not below the default) gets none.

## 2. The size it asks for
Held to the editor's limits before asking: the width to its range; the height to the aspect, or with
`editorFreeAspect` to at least the minimum width's height at that aspect - the same rules the wrappers apply.

## 3. Following the pointer
From the press: the pointer's movement in screen points since then, added to the size at the press - so the grip
stays under the pointer whatever the window does in between. A runtime class, named from the image's address, as
the panel view is, so two SynthLib plug-ins in one host do not share it.

The Windows VST3 view has no grip yet.

## 4. The cursor
On macOS 15 and later the system's own bottom-right frame-resize cursor (`frameResizeCursorFromPosition:
inDirections:`, both directions). Before 15 there is no public diagonal resize cursor, so a crosshair - the builds
target macOS 12.

## 5. Showing the cursor in any host
A cursor rect is applied only by a window that manages them while it is active, and an AU window in Ableton Live
showed none over the grip (the VST3, whose grip sits in the host's own view, did). So the grip also keeps a tracking
area that is active whether or not its window is key: entering, moving over or dragging it sets its cursor, leaving
sets the arrow back. The cursor rect stays for hosts that do apply them.
