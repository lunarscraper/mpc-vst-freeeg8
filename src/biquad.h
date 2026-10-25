/* RBJ cookbook biquad, Transposed Direct Form II, stereo.
 * Ported to C from FreeEQ8 (GareBear99/FreeEQ8, Source/DSP/Biquad.h, GPL-3.0) -- see src/VENDORED.md.
 * The coefficient formulas are FreeEQ8's, including its v2.3.1 shelf fix (shelf slope S clamped to RBJ's (0, 1]
 * domain, so a shelf can never produce NaN or overshoot). */
#pragma once
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

enum { BQ_BELL = 0, BQ_LOW_SHELF, BQ_HIGH_SHELF, BQ_HIGH_PASS, BQ_LOW_PASS, BQ_BAND_PASS, BQ_NTYPES };

typedef struct {
    double b0, b1, b2, a1, a2;
    double z1[2], z2[2];
} biquad_t;

static inline double bq_clamp(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline void bq_reset(biquad_t *f) { f->z1[0] = f->z1[1] = f->z2[0] = f->z2[1] = 0.0; }

static inline void bq_pass(biquad_t *f) { f->b0 = 1; f->b1 = f->b2 = f->a1 = f->a2 = 0; }

static inline double bq_run(biquad_t *f, int ch, double x) {
    const double y = f->b0 * x + f->z1[ch];
    f->z1[ch] = f->b1 * x - f->a1 * y + f->z2[ch];
    f->z2[ch] = f->b2 * x - f->a2 * y;
    return y;
}

static void bq_set(biquad_t *f, int type, double sr, double freq, double q, double gain_db) {
    freq = bq_clamp(freq, 10.0, sr * 0.45);
    q = bq_clamp(q, 0.1, 24.0);
    const double A = pow(10.0, gain_db / 40.0);
    const double w0 = 2.0 * M_PI * (freq / sr);
    const double cw = cos(w0), sw = sin(w0);
    const double alpha = sw / (2.0 * q);
    double b0, b1, b2, a0, a1, a2;
    switch (type) {
    default:
    case BQ_BELL:
        b0 = 1.0 + alpha * A; b1 = -2.0 * cw; b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A; a1 = -2.0 * cw; a2 = 1.0 - alpha / A;
        break;
    case BQ_LOW_PASS:
        b0 = (1.0 - cw) * 0.5; b1 = 1.0 - cw; b2 = (1.0 - cw) * 0.5;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    case BQ_HIGH_PASS:
        b0 = (1.0 + cw) * 0.5; b1 = -(1.0 + cw); b2 = (1.0 + cw) * 0.5;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    case BQ_LOW_SHELF: {
        const double S = bq_clamp(q / 2.0, 0.1, 1.0);
        const double aS = sw / 2.0 * sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double t = 2.0 * sqrt(A) * aS;
        b0 = A * ((A + 1) - (A - 1) * cw + t);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - t);
        a0 = (A + 1) + (A - 1) * cw + t;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - t;
        break;
    }
    case BQ_HIGH_SHELF: {
        const double S = bq_clamp(q / 2.0, 0.1, 1.0);
        const double aS = sw / 2.0 * sqrt((A + 1.0 / A) * (1.0 / S - 1.0) + 2.0);
        const double t = 2.0 * sqrt(A) * aS;
        b0 = A * ((A + 1) + (A - 1) * cw + t);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - t);
        a0 = (A + 1) - (A - 1) * cw + t;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - t;
        break;
    }
    case BQ_BAND_PASS:   /* constant 0 dB peak; gain ignored */
        b0 = alpha; b1 = 0.0; b2 = -alpha;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    }
    f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0;
    f->a1 = a1 / a0; f->a2 = a2 / a0;
}
