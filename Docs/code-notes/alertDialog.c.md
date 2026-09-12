# alertDialog.c notes

The longer comments from `alertDialog.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `wrap_message()`

Greedy word-wrap into messageLine[], measured the same way render_text() will draw it (the same
technique bankBrowser.cpp uses for its own message text). A '\n' is a HARD break, and two in a
row leave a deliberate blank line — without that the newline is swallowed into whichever word it
touches and two sentences render as "disagree.Your edits", with nothing to show where one
thought ended and the next began.

## 2. in `render_alert_dialog()`

The bank picker's dropdown is opened (from handle_alert_dialog_click()) on top of this modal
panel — the app's own render_context_menu() call elsewhere in the frame may run before or
after render_alert_dialog(), so re-invoking it here (a harmless no-op redraw when it's not
this dialog's own picker that's open) guarantees the flyout always ends up painted last.
