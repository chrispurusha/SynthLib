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
the rate. The stored loop end is the frame before the loop's last.

## 4. Reading on demand
Opening a bank reads only the `.exb`. A preset's samples are read the first time any preset names them and kept by
the bank, shared by every preset and zone that uses them, and freed with the bank.
