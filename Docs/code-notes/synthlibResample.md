# synthlibResample.c - fixed-ratio resampling

For an engine that runs at one rate of its own, whatever the host asks for.

## 1. The filter
A Kaiser-windowed sinc, 48 input samples either side of the output point, widened in proportion when converting down. 512 phases, the output blended between the two nearest, each phase normalised to unity at DC. The passband ends at 0.952 of the lower Nyquist, with the images about 90 dB down by that Nyquist. Latency is the half width, in output samples. The ring holds its samples twice, so the window is contiguous wherever it starts.
