# emuBank.c

E-mu Emulator X banks, read for a sampler to play. Moved here from G2Alike's Sampler reader (`nordSample.c`,
its notes §11) for UltraAlike; G2Alike still has its own copy until it moves onto this one.

## 1. Files
A bank is a `.exb` file (usually inside a folder of the same name) with a `SamplePool` folder beside it holding one
`.ebl` per sample, named after the bank: `<bank>SL001.ebl`, `SL002`... Both are IFF (`FORM`, big-endian tags and
lengths). Public references: the `ebl-reading` project (MIT) for the `.ebl` sample, and mpc2emu's E4B notes for the
E4 layouts these grew from.

## 2. Presets, voices, zones
The bank holds presets (`E5P1`), each a tree of chunks: a header, then a `LIST E5VL` of voices (`E5V1`). In a voice,
before its zone list, the first two `ETW` windows are its key and velocity ranges (byte 4 the low, byte 7 the high).
Its `LIST E5ZL` holds zones: a `Zhdr` - the sample number (u16 at 4, 1-based, the `SLnnn` of its file) and the root
key (byte 10) - and windows of its own that narrow the voice's. Only presets holding zones are counted; a bank also
carries empty ones that play nothing. A preset's name is the UTF-16 string 12 bytes into its `Phdr`, kept as ASCII
(non-ASCII becomes "?"), trailing spaces cut. A voice's tuning, level, pan, envelopes, filter, LFOs and cords are
not read yet.

## 3. Samples
A sample (`E5S1`): offsets in its header count from 2 bytes into the chunk - eight little-endian u32 at 0x48: start
L, start R, end L, end R, loop start L and R, loop end L and R, all byte offsets of 16-bit frames, the ends
inclusive; the left channel whole, then the right; then the rate (u32 at 0x68) and options (u16 at 0x6e, bit 0 the
loop). The rate is not a round number (43569 to 44053 Hz in one bank) and it is the one to play at: the tuning is in
the rate. The stored loop end is the loop's last frame: over four banks (2026-11-14) the seam - the loop's last frame
followed by its first - is smoothest there in three (Soul Oohs, Guitar Mutes, Saw Basses; one frame later it ticks,
which is how it was found), and nearly so in the fourth (Celtic Harp, where one frame later was marginally smoother -
the rule G2Alike's copy first took).

## 4. Reading on demand
Opening a bank reads only the `.exb`. A preset's samples are read the first time any preset names them and kept by
the bank, shared by every preset and zone that uses them, and freed with the bank.

## 5. Envelopes
A voice holds three `E5Ev` chunks before its zones - the amplifier's first, then the filter's and the auxiliary
envelope's, as on the E4. Each is 64 bytes: a 10-byte header (version 2), then six stages of 9 bytes, a big-endian
float time in seconds, a float level in percent and a byte - Attack 1 and 2, Decay 1 and 2, Release 1 and 2. All
1,549 distinct envelopes in one 11-bank set read this way. Only the amplifier's is kept so far, on each zone; the
header's other bytes and each stage's byte are not known.

## 6. A bank folder
Emulator X keeps each bank as a folder with the bank's own name - `Name.exb/` holding `Name.exb` and `SamplePool/` -
which a browser shows as the bank. `emu_bank_open()` given that folder opens the bank inside it, so a folder and the
file both work; sample paths are then found from the file's place as before (§1).

## 7. The stage float is a rate, not seconds
Read as seconds it made every envelope far too short (a factory piano decayed in 0.4 s). Across 1,383 banks and
387,000 amplifier envelopes the float never exceeds 1.0, and all but two values are at most 0.5: it is a fraction of
a scale, not a time. Read as the EOS rate (0-127) over 254 - 0.5 is rate 127 - the factory content behaves as it
should: a piano's Decay 2 0.4113 is rate 104 (about 24 s to silence on the E5000), its Release 0.1848 rate 47 (about
1 s), a drone's release 0.464 rate 118. The values are not exact multiples of 1/254 (1,933 distinct), so Emulator X
holds a continuous rate or converts by a slightly different curve; not yet confirmed against Emulator X itself.
Each stage's trailing byte has been 0 in every envelope looked at.

## 8. EOS banks (.E4B)
An E-mu EOS 4 bank (E4XT, E5000, E6400 Ultra and their kin) is one file holding its presets and its samples: `FORM`
`E4B0`, a table of contents (`TOC1`, 32-byte entries of type, length, offset and index; an entry whose offset does not
land on its own type is stale and passed over; older banks have none and are walked chunk by chunk), then chunks of a
type, a length counting a 2-byte index, the index, and the body. Some converters count the chunk's own header in its
length, so the last chunk claims 8 bytes past the end of the file: its length is clipped to the file.

- **E4P1**, a preset: its name in the first 16 bytes, the voice count at 0x12, voices from 0x52. A voice is a 284-byte
  header and its 22-byte zones; the header's first word is the voice's whole size. Keys low/high at 0x0c/0x0f,
  velocities at 0x10/0x13; transpose, coarse and fine tune (1/64 semitone) at 0x20-0x22; the amp envelope at 0x6c,
  six stages of a rate (0-127, 0 the fastest) and a level (0-127, shown as percent). A zone: keys at 0/3, velocities
  at 4/7, the sample's index at 8, fine tune at 10 (1/64 semitone, signed), root key at 12. A zone plays within its own
  and its voice's ranges; its tuning is the voice's transpose + coarse + fine plus its own, in `tEmuZone.tune`.
- **E3S1**, a sample, found by its index: a 16-byte name, then little-endian byte offsets from the name for start L
  and R, end L and R (exclusive), loop start L and R, loop end L and R, the rate at 0x34, a pitch word at 0x38 and flags
  at 0x3a (0x01 looped, 0x20 left, 0x40 right - both for stereo). The frames are 16-bit little-endian, each channel a
  block of its own.
- **Tuning.** EOS writes an untuned sample's pitch word as trunc(768 log2(rate / 44053)), in 1/64 semitone; a sample
  retuned on the sampler holds a different word. So a sample plays at its rate times 2^((word - that) / 768): exactly
  its rate when untuned, retuned when not. Whether some one-step differences are rounding rather than retuning is to
  be confirmed on an E5000.
- **Envelopes** are handed on as Emulator X stores them (§7), the rate over 254 and the level as percent, so one path
  reads both formats.
- A preset counts only when one of its zones names a sample the bank holds - some banks carry text-only presets
  ("Credits"), others zones whose samples are missing. A bank holds up to 1000 presets; a preset can hold thousands of
  zones (2,368 seen), hence EMU_BANK_MAX_ZONES 4096.

Checked (2026-11-21) against an independent reader of the format on 400 random banks of a library of 18,000: every
readable bank the same presets, zones, samples and loops, stereo included; 2,000 more read with no failure.
