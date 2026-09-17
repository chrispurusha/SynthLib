# prefs.cpp notes

The longer comments from `prefs.cpp`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. file scope

Backing storage for the pointer prefs_get_string()/cache_get_string() return. THREAD-LOCAL: the
patch-name cache is written and read from the USB thread while the UI thread is reading window
geometry and the recent-file list, so a single shared buffer would hand one thread a pointer to
a string the other is in the middle of reassigning.

## 2. file scope

One lock for both stores. Recursive because cache_get_string() falls back to prefs_get_string().
Contention is irrelevant here — these are a handful of map lookups and an occasional small file
write — and the alternative is confining the whole store to one thread, which the name cache
(USB thread) and the window geometry (UI thread) between them rule out.

## 3. file scope

Two stores, same format, separate files. Settings (prefs.txt) are small and change when the user
changes something; the patch-name cache (cache.txt) is bulky and is rewritten far more often —
potentially on every reply during a name sweep. Keeping them apart means cache churn can never
cost the user their settings, and lets the cache be deleted wholesale without touching prefs.

## 4. `save()`

Written to a sibling temp file and renamed into place, rather than truncating the real file and
rewriting it. rename() is atomic within a directory, so a crash or a kill mid-save leaves the
previous file fully intact instead of a half-written one. This used to open the live file with
std::ios::trunc, which put every saved setting at risk on every single write.

## 5. in `save()`

A save writes only the keys THIS process has set (`written`), laid over the file as it is on disk at
that moment (2026-09-17). Until then it wrote its whole in-memory copy, so two processes sharing a
file - the G2-Edit application and its G2 Alike plug-in, which are often open together - quietly
threw away each other's settings, last writer wins for every key. Now it is last writer wins per
key, and the copy in memory is refreshed from disk as a side effect. Re-reading costs one small file
per save, and saves are rare.

## 6. `prefs_set_string_in()` / `prefs_get_string_from()`

One key in a named app's prefs.txt, for a setting two programs share. Both go to the file on every
call and keep nothing, so each sees what the other last wrote. G2 Alike keeps the file browser's
last folder in G2-Edit's file this way, so the plug-in and the application open where either left
off, while every other plug-in setting stays in its own file.
