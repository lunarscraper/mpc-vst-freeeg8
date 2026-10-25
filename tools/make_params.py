#!/usr/bin/env python3
"""Writes vst/params.json and vst/presets.json (run from the repo root: python3 tools/make_params.py).
The parameter ORDER is the VST index that projects and skins bind to: append only (vst/params.lock checks it).
Frequency and Q are 0..1 knob positions on a log scale; src/eq8.c maps them (20 Hz..20 kHz, Q 0.1..24)."""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
VST = os.path.join(HERE, "..", "vst")

TYPES = ["BELL", "LOW SHELF", "HIGH SHELF", "LOW CUT", "HIGH CUT", "BAND PASS"]
SLOPES = ["12 dB", "24 dB", "48 dB"]
LISTEN = ["OFF", "1", "2", "3", "4", "5", "6", "7", "8"]


def fpos(hz):
    return round(math.log(hz / 20.0) / math.log(1000.0), 6)


def qpos(q):
    return round(math.log(q / 0.1) / math.log(240.0), 6)


# defaults = the engine's own (src/eq8.c eq_create): every band that is on sits at 0 dB
DEFAULTS = [
    (False, "LOW CUT", 30, 0, 0.71, "24 dB"),
    (True, "LOW SHELF", 100, 0, 0.71, "12 dB"),
    (True, "BELL", 250, 0, 0.71, "12 dB"),
    (True, "BELL", 500, 0, 0.71, "12 dB"),
    (True, "BELL", 1000, 0, 0.71, "12 dB"),
    (True, "BELL", 2500, 0, 0.71, "12 dB"),
    (True, "HIGH SHELF", 8000, 0, 0.71, "12 dB"),
    (False, "HIGH CUT", 18000, 0, 0.71, "12 dB"),
]


def params():
    out = []
    for i, (on, typ, f, g, q, slope) in enumerate(DEFAULTS, 1):
        out += [
            {"key": "b%d_on" % i, "name": "Band %d On" % i, "options": ["OFF", "ON"], "default": 1 if on else 0},
            {"key": "b%d_type" % i, "name": "Band %d Type" % i, "options": TYPES, "default": TYPES.index(typ),
             "refresh_display": True},
            {"key": "b%d_freq" % i, "name": "Band %d Freq" % i, "min": 0.0, "max": 1.0, "default": fpos(f),
             "dynamic_display": True},
            {"key": "b%d_gain" % i, "name": "Band %d Gain" % i, "min": -24.0, "max": 24.0, "default": float(g),
             "unit": "dB", "dynamic_display": True},
            {"key": "b%d_q" % i, "name": "Band %d Q" % i, "min": 0.0, "max": 1.0, "default": qpos(q),
             "dynamic_display": True},
            {"key": "b%d_slope" % i, "name": "Band %d Slope" % i, "options": SLOPES, "default": SLOPES.index(slope)},
        ]
    out += [
        {"key": "out", "name": "Output", "min": -24.0, "max": 24.0, "default": 0.0, "unit": "dB", "dynamic_display": True},
        {"key": "scale", "name": "Scale", "min": 0.0, "max": 200.0, "default": 100.0, "unit": "%", "dynamic_display": True},
        {"key": "listen", "name": "Listen", "options": LISTEN, "default": 0},
    ]
    return out


def preset(name, bands, out=0.0, scale=100.0):
    """bands: {band number: (on, type, Hz, gain dB, Q, slope)}; bands left out are the defaults"""
    vals = {}
    for i in range(1, 9):
        on, typ, f, g, q, slope = bands.get(i, DEFAULTS[i - 1])
        vals.update({
            "b%d_on" % i: "ON" if on else "OFF", "b%d_type" % i: typ, "b%d_freq" % i: fpos(f),
            "b%d_gain" % i: float(g), "b%d_q" % i: qpos(q), "b%d_slope" % i: slope,
        })
    vals.update({"out": float(out), "scale": float(scale), "listen": "OFF"})
    return {"name": name, "values": vals}


ON, OFF = True, False
PRESETS = [
    preset("Neutral", {}),
    preset("Kick Clean", {
        1: (ON, "LOW CUT", 30, 0, 0.71, "24 dB"),
        3: (ON, "BELL", 300, -4, 1.4, "12 dB"),
        6: (ON, "BELL", 3500, 2, 1.0, "12 dB"),
    }),
    preset("Psy Bass", {
        1: (ON, "LOW CUT", 35, 0, 0.71, "24 dB"),
        3: (ON, "BELL", 250, -3, 1.2, "12 dB"),
        8: (ON, "HIGH CUT", 9000, 0, 0.71, "12 dB"),
    }),
    preset("Lead Forward", {
        1: (ON, "LOW CUT", 180, 0, 0.71, "24 dB"),
        4: (ON, "BELL", 400, -3, 1.0, "12 dB"),
        6: (ON, "BELL", 3000, 3, 1.0, "12 dB"),
        7: (ON, "HIGH SHELF", 10000, 1.5, 0.71, "12 dB"),
    }),
    preset("Pad Make Room", {
        1: (ON, "LOW CUT", 220, 0, 0.71, "24 dB"),
        4: (ON, "BELL", 500, -2, 0.8, "12 dB"),
        6: (ON, "BELL", 2500, -3, 0.7, "12 dB"),
        7: (ON, "HIGH SHELF", 9000, -2, 0.71, "12 dB"),
    }),
    preset("Hi-Hats", {
        1: (ON, "LOW CUT", 400, 0, 0.71, "24 dB"),
        5: (ON, "BELL", 1200, -2, 1.0, "12 dB"),
        7: (ON, "HIGH SHELF", 10000, 2, 0.71, "12 dB"),
    }),
    preset("Voice", {
        1: (ON, "LOW CUT", 100, 0, 0.71, "24 dB"),
        3: (ON, "BELL", 300, -3, 1.2, "12 dB"),
        6: (ON, "BELL", 4000, 2.5, 1.0, "12 dB"),
    }),
    preset("Master Gentle", {
        1: (ON, "LOW CUT", 25, 0, 0.71, "24 dB"),
        2: (ON, "LOW SHELF", 60, 1, 0.71, "12 dB"),
        3: (ON, "BELL", 300, -1, 0.8, "12 dB"),
        7: (ON, "HIGH SHELF", 12000, 1.5, 0.71, "12 dB"),
    }),
    preset("Telephone", {
        1: (ON, "LOW CUT", 400, 0, 0.71, "48 dB"),
        5: (ON, "BELL", 1500, 4, 1.2, "12 dB"),
        8: (ON, "HIGH CUT", 3000, 0, 0.71, "48 dB"),
    }),
    preset("DJ Low Cut", {
        1: (ON, "LOW CUT", 200, 0, 1.2, "48 dB"),
    }),
]

if __name__ == "__main__":
    json.dump({"name": "Free EQ8", "params": params()}, open(os.path.join(VST, "params.json"), "w"), indent=1)
    json.dump({"presets": PRESETS}, open(os.path.join(VST, "presets.json"), "w"), indent=1)
    print("params.json: %d params, presets.json: %d presets" % (len(params()), len(PRESETS)))
