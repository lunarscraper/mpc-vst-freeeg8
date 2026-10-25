/* Free EQ8 for MPC OS: an 8-band parametric EQ as an insert effect, built on mpc-vst-plugins' wrapper (wrapper/engine.h).
 * The filters are FreeEQ8's RBJ biquads (src/biquad.h); this file is the band logic for the MPC:
 *   - 8 bands: on, type (bell, low/high shelf, low/high cut, band pass), frequency, gain, Q, slope (cuts: 12/24/48 dB/oct)
 *   - LISTEN: hear one band's range alone (a band pass at its frequency), to find what crowds a range
 *   - OUTPUT gain and SCALE (all band gains at once, 0-200 %)
 * Frequency and Q are 0..1 knob positions on a log scale (the wrapper has no log knobs); the engine maps them and gives
 * the text through "<key>_display". Parameter changes glide (no zipper noise); switching a band or its type cross-fades.
 * 44.1 kHz, interleaved int16 stereo, 128-frame blocks. GPL-3.0-or-later. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "biquad.h"
#include "engine.h"

#define SR 44100.0
#define NBANDS 8
#define MAXST 4
#define TICK 16                 /* coefficient update interval while gliding (FreeEQ8: 16 samples) */
#define GLIDE 0.08              /* per tick: ~4 ms time constant */
#define FADE_STEP (1.0 / 441.0) /* band on/off and type change: 10 ms cross-fade */

#define F_MIN 20.0
#define F_RATIO 1000.0          /* 20 Hz .. 20 kHz */
#define Q_MIN 0.1
#define Q_RATIO 240.0           /* 0.1 .. 24 */

typedef struct {
    /* parameters as set */
    int on, type, slope;
    double fpos, gain, qpos;
    /* gliding values: log2 frequency, gain dB, ln Q */
    double lf, g, lq;
    int nst, cur_type;
    biquad_t st[MAXST];
    double amt;                 /* 0..1: how much of the band is in the signal (fades) */
    int fade_out;               /* type/slope changed: fade out, switch, fade in */
} band_t;

typedef struct {
    band_t b[NBANDS];
    double out_db, scale;       /* scale in % */
    double scale_cur;           /* gliding scale */
    int listen;                 /* 0 = off, 1..8 */
    double out_lin;             /* gliding output gain */
    biquad_t lis;               /* LISTEN band pass */
    int lis_cur;                /* band the listen filter follows (0 = off) */
    double lis_amt;             /* 0..1 cross-fade between EQ output and the listen band */
    int tick;
} eq_t;

static double pos_to_freq(double x) { return F_MIN * pow(F_RATIO, bq_clamp(x, 0, 1)); }
static double pos_to_q(double x) { return Q_MIN * pow(Q_RATIO, bq_clamp(x, 0, 1)); }
static double freq_to_pos(double f) { return log(f / F_MIN) / log(F_RATIO); }
static double q_to_pos(double q) { return log(q / Q_MIN) / log(Q_RATIO); }

static int is_cut(int type) { return type == BQ_HIGH_PASS || type == BQ_LOW_PASS; }

/* Butterworth stage Q values for 2, 4 and 8 poles; the user's Q scales the most resonant stage, so Q 0.71 is a plain
 * Butterworth cut and a higher Q adds a resonance at the corner. */
static const double BW1[1] = {0.70710678};
static const double BW2[2] = {0.54119610, 1.30656296};
static const double BW4[4] = {0.50979558, 0.60134489, 0.89997622, 2.56291545};

static void band_coeffs(eq_t *e, band_t *b) {
    const double f = exp2(b->lf), q = exp(b->lq);
    const double g = b->g * e->scale_cur * 0.01;
    if (is_cut(b->cur_type)) {
        const double *bw = b->nst == 4 ? BW4 : b->nst == 2 ? BW2 : BW1;
        for (int s = 0; s < b->nst; s++) {
            double qs = bw[s];
            if (s == b->nst - 1) qs *= q / 0.70710678;
            bq_set(&b->st[s], b->cur_type, SR, f, qs, 0.0);
        }
    } else {
        bq_set(&b->st[0], b->cur_type, SR, f, q, g);
    }
}

static int stages_for(int type, int slope) {
    if (!is_cut(type)) return 1;
    return slope == 2 ? 4 : slope == 1 ? 2 : 1;
}

static void band_init(band_t *b, int on, int type, double f, double gain, double q, int slope) {
    memset(b, 0, sizeof *b);
    b->on = on; b->type = type; b->slope = slope;
    b->fpos = freq_to_pos(f); b->gain = gain; b->qpos = q_to_pos(q);
}

static void band_snap(eq_t *e, band_t *b) {   /* jump to the set values (load, restore) */
    b->lf = log2(pos_to_freq(b->fpos));
    b->g = b->gain;
    b->lq = log(pos_to_q(b->qpos));
    b->cur_type = b->type;
    b->nst = stages_for(b->type, b->slope);
    for (int s = 0; s < MAXST; s++) bq_reset(&b->st[s]);
    band_coeffs(e, b);
    b->amt = b->on ? 1.0 : 0.0;
    b->fade_out = 0;
}

static void *eq_create(const char *dir) {
    (void)dir;
    eq_t *e = calloc(1, sizeof *e);
    if (!e) return NULL;
    /* defaults: a quiet start -- every band that is on sits at 0 dB, so the plugin is transparent */
    band_init(&e->b[0], 0, BQ_HIGH_PASS, 30, 0, 0.71, 1);
    band_init(&e->b[1], 1, BQ_LOW_SHELF, 100, 0, 0.71, 1);
    band_init(&e->b[2], 1, BQ_BELL, 250, 0, 0.71, 1);
    band_init(&e->b[3], 1, BQ_BELL, 500, 0, 0.71, 1);
    band_init(&e->b[4], 1, BQ_BELL, 1000, 0, 0.71, 1);
    band_init(&e->b[5], 1, BQ_BELL, 2500, 0, 0.71, 1);
    band_init(&e->b[6], 1, BQ_HIGH_SHELF, 8000, 0, 0.71, 1);
    band_init(&e->b[7], 0, BQ_LOW_PASS, 18000, 0, 0.71, 1);
    e->out_db = 0; e->scale = 100; e->listen = 0;
    e->out_lin = 1.0;
    e->scale_cur = 100;
    for (int i = 0; i < NBANDS; i++) band_snap(e, &e->b[i]);
    bq_pass(&e->lis);
    return e;
}

static void eq_destroy(void *p) { free(p); }

static void eq_midi(void *p, const uint8_t *m, int len) { (void)p; (void)m; (void)len; }

/* "b3_freq" -> band 2, field "freq" */
static int band_key(const char *key, const char **field) {
    if (key[0] != 'b' || key[1] < '1' || key[1] > '8' || key[2] != '_') return -1;
    *field = key + 3;
    return key[1] - '1';
}

static void set_one(eq_t *e, const char *key, const char *val, int snap) {
    const char *fld;
    const double v = atof(val);
    const int bi = band_key(key, &fld);
    if (bi >= 0) {
        band_t *b = &e->b[bi];
        if (!strcmp(fld, "on")) b->on = (int)v ? 1 : 0;
        else if (!strcmp(fld, "type")) {
            int t = (int)v;
            if (t < 0 || t >= BQ_NTYPES) t = 0;
            if (t != b->type) { b->type = t; b->fade_out = 1; }
        } else if (!strcmp(fld, "slope")) {
            int s = (int)v;
            if (s < 0 || s > 2) s = 0;
            if (s != b->slope) { b->slope = s; if (is_cut(b->type)) b->fade_out = 1; }
        } else if (!strcmp(fld, "freq")) b->fpos = bq_clamp(v, 0, 1);
        else if (!strcmp(fld, "gain")) b->gain = bq_clamp(v, -24, 24);
        else if (!strcmp(fld, "q")) b->qpos = bq_clamp(v, 0, 1);
        if (snap) band_snap(e, b);
        return;
    }
    if (!strcmp(key, "out")) e->out_db = bq_clamp(v, -24, 24);
    else if (!strcmp(key, "scale")) e->scale = bq_clamp(v, 0, 200);
    else if (!strcmp(key, "listen")) { int l = (int)v; e->listen = l < 0 || l > 8 ? 0 : l; }
}

/* "state": every value as key=value;... (the plugin's chunk, saved with the project and MPC presets) */
static void set_state(eq_t *e, const char *s) {
    char key[32], val[32];
    while (*s) {
        const char *eq = strchr(s, '='), *end = strchr(s, ';');
        if (!eq || (end && end < eq)) break;
        if (!end) end = s + strlen(s);
        size_t kl = (size_t)(eq - s), vl = (size_t)(end - eq - 1);
        if (kl < sizeof key && vl < sizeof val) {
            memcpy(key, s, kl); key[kl] = 0;
            memcpy(val, eq + 1, vl); val[vl] = 0;
            set_one(e, key, val, 0);
        }
        s = *end ? end + 1 : end;
    }
    e->scale_cur = e->scale;
    for (int i = 0; i < NBANDS; i++) band_snap(e, &e->b[i]);
    e->out_lin = pow(10.0, e->out_db / 20.0);
}

static void eq_set(void *p, const char *key, const char *val) {
    eq_t *e = p;
    if (!strcmp(key, "state")) set_state(e, val);
    else set_one(e, key, val, 0);
}

static int fmt_freq(char *buf, int n, double f) {
    if (f < 100) return snprintf(buf, n, "%.1f Hz", f);
    if (f < 1000) return snprintf(buf, n, "%.0f Hz", f);
    if (f < 10000) return snprintf(buf, n, "%.2f kHz", f / 1000);
    return snprintf(buf, n, "%.1f kHz", f / 1000);
}

static int fmt_db(char *buf, int n, double db) {
    if (fabs(db) < 0.05) return snprintf(buf, n, "0.0 dB");
    return snprintf(buf, n, "%+.1f dB", db);
}

static int eq_get(void *p, const char *key, char *buf, int n) {
    eq_t *e = p;
    if (!strcmp(key, "state")) {
        int len = 0;
        for (int i = 0; i < NBANDS && len < n; i++) {
            band_t *b = &e->b[i];
            len += snprintf(buf + len, n - len, "b%d_on=%d;b%d_type=%d;b%d_freq=%.6f;b%d_gain=%.4f;b%d_q=%.6f;b%d_slope=%d;",
                            i + 1, b->on, i + 1, b->type, i + 1, b->fpos, i + 1, b->gain, i + 1, b->qpos, i + 1, b->slope);
        }
        if (len < n) len += snprintf(buf + len, n - len, "out=%.4f;scale=%.4f;listen=%d", e->out_db, e->scale, e->listen);
        return len < n ? len : 0;
    }
    const char *fld;
    const int bi = band_key(key, &fld);
    if (bi >= 0) {
        band_t *b = &e->b[bi];
        if (!strcmp(fld, "on")) return snprintf(buf, n, "%d", b->on);
        if (!strcmp(fld, "type")) return snprintf(buf, n, "%d", b->type);
        if (!strcmp(fld, "slope")) return snprintf(buf, n, "%d", b->slope);
        if (!strcmp(fld, "freq")) return snprintf(buf, n, "%.6f", b->fpos);
        if (!strcmp(fld, "gain")) return snprintf(buf, n, "%.4f", b->gain);
        if (!strcmp(fld, "q")) return snprintf(buf, n, "%.6f", b->qpos);
        if (!strcmp(fld, "freq_display")) return fmt_freq(buf, n, pos_to_freq(b->fpos));
        if (!strcmp(fld, "gain_display")) {
            if (is_cut(b->type) || b->type == BQ_BAND_PASS) return snprintf(buf, n, "--");
            return fmt_db(buf, n, b->gain);
        }
        if (!strcmp(fld, "q_display")) return snprintf(buf, n, "%.2f", pos_to_q(b->qpos));
        return 0;
    }
    if (!strcmp(key, "out")) return snprintf(buf, n, "%.4f", e->out_db);
    if (!strcmp(key, "out_display")) return fmt_db(buf, n, e->out_db);
    if (!strcmp(key, "scale")) return snprintf(buf, n, "%.4f", e->scale);
    if (!strcmp(key, "scale_display")) return snprintf(buf, n, "%.0f %%", e->scale);
    if (!strcmp(key, "listen")) return snprintf(buf, n, "%d", e->listen);
    return 0;
}

/* one coefficient tick: glide every band toward its set values, run the fades' state changes */
static void update_tick(eq_t *e) {
    const int scale_moving = fabs(e->scale - e->scale_cur) > 1e-3;
    if (scale_moving) e->scale_cur += (e->scale - e->scale_cur) * GLIDE;
    for (int i = 0; i < NBANDS; i++) {
        band_t *b = &e->b[i];
        if (b->fade_out && b->amt <= 0.0) {   /* faded out: switch type/slope with clean filter state */
            b->cur_type = b->type;
            b->nst = stages_for(b->type, b->slope);
            for (int s = 0; s < MAXST; s++) bq_reset(&b->st[s]);
            b->lf = log2(pos_to_freq(b->fpos)); b->g = b->gain; b->lq = log(pos_to_q(b->qpos));
            band_coeffs(e, b);
            b->fade_out = 0;
            continue;
        }
        if (!b->on && b->amt <= 0.0) continue;
        const double tf = log2(pos_to_freq(b->fpos)), tq = log(pos_to_q(b->qpos));
        if (fabs(tf - b->lf) > 1e-4 || fabs(b->gain - b->g) > 1e-3 || fabs(tq - b->lq) > 1e-4 || scale_moving) {
            b->lf += (tf - b->lf) * GLIDE;
            b->g += (b->gain - b->g) * GLIDE;
            b->lq += (tq - b->lq) * GLIDE;
            band_coeffs(e, b);
        }
    }
    /* LISTEN follows its band's frequency and Q (shelves and cuts: a medium width) */
    if (e->listen && e->lis_cur == e->listen) {
        band_t *b = &e->b[e->listen - 1];
        const double q = (b->cur_type == BQ_BELL || b->cur_type == BQ_BAND_PASS) ? exp(b->lq) : 1.0;
        bq_set(&e->lis, BQ_BAND_PASS, SR, exp2(b->lf), q, 0.0);
    }
}

static void eq_process(void *p, const int16_t *in, int16_t *out, int frames) {
    eq_t *e = p;
    const double out_target = pow(10.0, e->out_db / 20.0);
    /* LISTEN switches through silence: fade the listen path out, retune, fade in */
    for (int i = 0; i < frames; i++) {
        if ((i % TICK) == 0) {
            if (e->lis_amt <= 0.0 && e->lis_cur != e->listen) {
                e->lis_cur = e->listen;
                bq_reset(&e->lis);
            }
            update_tick(e);
            e->tick = (e->tick + 1) & 0xffff;
        }
        double x[2] = {in[2 * i] * (1.0 / 32768.0) + 1e-20, in[2 * i + 1] * (1.0 / 32768.0) + 1e-20};
        double y[2] = {x[0], x[1]};
        for (int k = 0; k < NBANDS; k++) {
            band_t *b = &e->b[k];
            const double tgt = (b->on && !b->fade_out) ? 1.0 : 0.0;
            if (b->amt != tgt) {
                b->amt += tgt > b->amt ? FADE_STEP : -FADE_STEP;
                if (b->amt < 0.0) b->amt = 0.0;
                if (b->amt > 1.0) b->amt = 1.0;
            }
            if (b->amt <= 0.0) continue;
            for (int c = 0; c < 2; c++) {
                double v = y[c];
                for (int s = 0; s < b->nst; s++) v = bq_run(&b->st[s], c, v);
                y[c] = b->amt >= 1.0 ? v : y[c] + (v - y[c]) * b->amt;
            }
        }
        /* LISTEN: the band's range of the input, alone */
        const double lt = (e->listen && e->lis_cur == e->listen) ? 1.0 : 0.0;
        if (e->lis_amt != lt) {
            e->lis_amt += lt > e->lis_amt ? FADE_STEP : -FADE_STEP;
            if (e->lis_amt < 0.0) e->lis_amt = 0.0;
            if (e->lis_amt > 1.0) e->lis_amt = 1.0;
        }
        if (e->lis_cur) {
            for (int c = 0; c < 2; c++) {
                const double l = bq_run(&e->lis, c, x[c]);
                y[c] += (l - y[c]) * e->lis_amt;
            }
        }
        e->out_lin += (out_target - e->out_lin) * 0.002;
        for (int c = 0; c < 2; c++) {
            double v = y[c] * e->out_lin * 32768.0;
            out[2 * i + c] = (int16_t)(v > 32767.0 ? 32767 : v < -32768.0 ? -32768 : lrint(v));
        }
    }
}

static void eq_render(void *p, int16_t *out, int frames) { (void)p; memset(out, 0, (size_t)frames * 4); }

static const mpc_engine_t ENGINE = {eq_create, eq_destroy, eq_midi, eq_set, eq_get, eq_render, eq_process, NULL};

const mpc_engine_t *mpc_engine(void) { return &ENGINE; }
