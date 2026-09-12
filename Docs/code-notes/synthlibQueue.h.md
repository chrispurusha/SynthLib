# synthlibQueue.h notes

The longer comments from `synthlibQueue.h`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `eRcv`

A generic, payload-agnostic thread-safe FIFO for passing fixed-size messages between two threads
(e.g. a UI thread and a device-comms thread). The queue copies payloadSize opaque bytes per message
and never interprets them, so the embedding app defines its own message struct and passes
sizeof(that struct) to msg_init(). One queue is fixed to one payload size.

Typical use: one queue per direction (e.g. gToUsbThread / gToGuiThread). The consumer either blocks
(eRcvWait — a dedicated worker thread) or polls (eRcvPoll — a render loop that must not block, then
wakes itself / is woken to drain). The mechanism has no app dependencies, so it lives in SynthLib.
