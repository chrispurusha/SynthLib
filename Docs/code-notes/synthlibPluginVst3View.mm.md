# synthlibPluginVst3View.mm notes

The longer comments from `synthlibPluginVst3View.mm`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

AN IPlugView AND NOTHING ELSE. The window's CONTENTS come from the plug-in's own createView(),
which hands back an NSView; everything here is the protocol a VST3 host speaks to a window.

The same NSView is what the Audio Unit wrapper's Cocoa view factory asks for, so a change to the
editor is made once and both formats show it.

## 2. file scope

EVERY OPEN EDITOR, for synthlib_plugin_request_resize() - which names an instance, and has to find
that instance's window among however many are open. This was one global frame, the last editor
opened, so with two tracks' editors open a resize asked for by one moved the other.

MAIN THREAD ONLY, like everything that touches it: a host attaches, removes and destroys views
there, and request_resize() is documented as main-thread.

## 3. file scope

RESTORE THE SIZE IN THE CONSTRUCTOR, not in attached(). getSize() is asked BEFORE
attached(), so a width recovered any later opens the window at the default and then
jumps it.

WIDTH ONLY IS STORED: with an aspect lock the height is derived from it, so keeping both
would be storing the same fact twice and inviting them to disagree.

THE PROJECT'S OWN WIDTH FIRST, when the controller restored one; then the machine-wide
preference; then the default.

## 4. file scope

RESIZED WITH ITS PARENT, ON EVERY FRAME OF A DRAG. A host calls onSize() when a drag ENDS,
while AppKit resizes the parent throughout it - so without the mask the contents sat at
their old size until the user let go, which is exactly what GenBridge's editor did until
this was put back. The two do not fight: onSize() then asks for a frame the view already has.

## 5. file scope

Remembered for next time. Written on every resize rather than on close, because a host is
under no obligation to tell a view it is going away in any particular order - to the
project, through the controller, and to the machine-wide preference as the starting point
for an instance that has never been opened.

## 6. file scope

THE ASPECT RATIO IS LOCKED when the descriptor asks for it, as the canvas application locks
its own window with glfwSetWindowAspectRatio().

That lock is what completes the scaling: a canvas scaled from WIDTH alone would, in a taller
window, simply uncover more rows rather than drawing larger. The application never shows that
because its window cannot be made taller without also becoming wider.

NEITHER EDGE IS THE AUTHORITY, AND THERE IS NO HISTORY. A host proposes the pointer's rect over
and over through a drag and applies what comes back, and in that rect the dimension the user is
NOT dragging still holds its old value. Following the width alone - this wrapper's first answer
- ignored a drag of the bottom edge entirely. Deciding which edge moved by comparing against the
size last answered - GenBridge's first two - made the reply to a rect depend on what had come
before it, and the window jumped and snapped back mid-drag.

Averaging the width the rect implies with the width its HEIGHT implies depends on nothing but
the rect in hand. It is idempotent, so an answer fed back comes back unchanged, and continuous,
so there is no branch to flip: dragging one edge moves the other at half rate for a step or two
and converges on exactly the size asked for. tools/vst3check in GenBridge holds it to both.

ROUNDED, NOT TRUNCATED. A host feeds each answer back in, so a truncation is not a one-off half
pixel - it is a step taken every time.
