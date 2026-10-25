# Vendored code

`src/biquad.h` is a C port of FreeEQ8's filter core:

- Upstream: https://github.com/GareBear99/FreeEQ8 (GPL-3.0)
- Commit: `11376c1c496569c975e4d195c3fe5fd44b53d415` (2026-08-01, v2.3.1)
- File: `Source/DSP/Biquad.h`

Taken over unchanged in substance: the RBJ cookbook coefficients for bell, low/high shelf, low/high pass and band
pass, Transposed Direct Form II with double-precision state, and the v2.3.1 shelf fix (shelf slope S = Q/2 clamped
to (0, 1], so shelves never go NaN or overshoot).

Changed locally:
- C instead of C++ (the MPC wrapper is C), one struct for both channels.

Not taken over (written for the MPC in `src/eq8.c` or left out on purpose):
- Band logic, smoothing and fades are new (`src/eq8.c`), modelled on FreeEQ8's `EQBand.h` (coefficients refreshed
  every 16 samples while a value glides).
- Cut slopes: FreeEQ8 cascades 1/2/4 identical stages; here the stages use Butterworth Q values (a clean 12/24/48 dB
  per octave cut, -3 dB at the set frequency) and the band's Q scales the most resonant stage. Bells and shelves are
  always one stage.
- Left out: linear phase (46 ms latency), oversampling, match EQ, dynamic EQ, per-band drive, mid/side, the
  spectrum display (MPC skins cannot draw curves), the ProEQ8 licence code.
