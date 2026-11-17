#!/usr/bin/env python3
"""Phase-duration stats for recent Harvest Temple CM kills, from GW2 Wingman.

Usage: python3 tools/wingman_phase_stats.py data/htcm_phase_durations.json [cache_dir]

Uses two public Wingman endpoints:
  /api/rev/boss/24375       -> current-era stats, including links to CM kills
  /api/getMetadata/<link>   -> per-log summary (phase durations, mechanics counts)
Wingman doesn't serve full timelines without a manual reparse, so this only
gives phase lengths, not when individual skills happen inside a phase.
"""
import json
import os
import re
import statistics
import sys
import time
import urllib.request

BASE = "https://gw2wingman.nevermindcreations.de"
UA = {"User-Agent": "htcm-helper-research/0.1"}
OUT = sys.argv[1]
CACHE = sys.argv[2] if len(sys.argv) > 2 else None

PHASE_ORDER = [
    "Full Fight",
    "Purification 1", "Jormag", "Primordus", "Kralkatorrik",
    "Purification 2", "Void Time Caster", "Mordremoth", "Giants", "Zhaitan",
    "Purification 3", "Void Saltspray Dragon", "Soo-Won", "Soo-Won 1",
    "Void Obliterator", "Void Goliath", "Soo-Won 2", "Purification 4",
]


def get_json(path):
    req = urllib.request.Request(BASE + path, headers=UA)
    with urllib.request.urlopen(req, timeout=60) as r:
        return json.load(r)


def metadata(link):
    if CACHE:
        p = os.path.join(CACHE, link + ".json")
        if os.path.exists(p):
            with open(p) as f:
                return json.load(f)
    d = get_json("/api/getMetadata/" + link)
    time.sleep(1)  # be polite
    if CACHE:
        os.makedirs(CACHE, exist_ok=True)
        with open(os.path.join(CACHE, link + ".json"), "w") as f:
            json.dump(d, f)
    return d


cm = get_json("/api/rev/boss/24375")["data"]["stats"]["cm"]
links = sorted(set(re.findall(r'"([^"]*\d{8}-\d{6}_harvsttmpl_kill)"', json.dumps(cm))))

logs = []
for link in links:
    d = metadata(link)
    if d.get("isCM") and d.get("success"):
        logs.append(d)

phases = {}
for name in PHASE_ORDER:
    v = [d["phases"][name] for d in logs if name in d.get("phases", {})]
    if v:
        phases[name] = {"n": len(v), "min": min(v), "median": statistics.median(v), "max": max(v)}

out = {
    "source": BASE + "/api/rev/boss/24375 (stats.cm) + /api/getMetadata",
    "era": cm.get("era"),
    "fetched": time.strftime("%Y-%m-%d"),
    "gw2_builds": sorted({d["gw2Build"] for d in logs}),
    "logs": [{"link": d["html"], "duration_s": d["durationMS"] / 1000, "start": d["timeStart"]} for d in logs],
    "phase_seconds": phases,
}
with open(OUT, "w") as f:
    json.dump(out, f, indent=1)
print(f"{len(logs)} CM kills -> {OUT}")
for name, s in phases.items():
    print(f"  {name:24s} n={s['n']:2d}  min {s['min']:6.1f}  median {s['median']:6.1f}  max {s['max']:6.1f}")
