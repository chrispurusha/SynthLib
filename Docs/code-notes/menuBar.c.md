# menuBar.c notes

The longer comments from `menuBar.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. in `render_menu_bar()`

AN OPEN MENU STAYS LIT; A HOVER DOES NOT COUNT WHILE THE POINTER IS CAPTURED. During a
dial drag the cursor is hidden and its reported position is a relative-delta accumulator,
free to drift anywhere - drag far enough and it wanders into the bar and lights an item
under a cursor that is not on screen. sOpenIndex is deliberately still honoured: a menu
the user actually opened should stay lit whatever the pointer is doing.
