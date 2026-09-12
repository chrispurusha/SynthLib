# alertDialog.h notes

The longer comments from `alertDialog.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `void()`

In-window replacement for NSAlert-based info/confirm/bank-number-picker dialogs, drawn with the
same GLFW/OpenGL primitives as fileBrowser.h/bankBrowser.h. Three flavours share one modal panel:
```
  show_alert()         - message + single OK button, no callback (fire and forget)
  show_confirm()       - message + Cancel/confirmLabel buttons
  show_bank_confirm()  - show_confirm() plus a "Bank N" picker button that opens a dropdown
                         (built on the app's own context-menu system, see contextMenu.h) for
                         choosing a bank number in [1, maxBank1Indexed]

```
The embedding app must call synthlib_host_init() (synthlibHost.h) once at startup, same as
contextMenu.h/fileBrowser.h require, and must, once per frame, call render_alert_dialog() (order
relative to its own render_context_menu() call doesn't matter — render_alert_dialog() re-invokes
render_context_menu() itself when the picker dropdown is open, so that flyout always paints over
this panel regardless of where the app's own call happens to sit). Route mouse-down through
handle_alert_dialog_mouse_down() and mouse-up through handle_alert_dialog_click() ahead of other
click handling; route key events through handle_alert_dialog_key(). All are safe to call
unconditionally — they no-op when alert_dialog_active() is false.
