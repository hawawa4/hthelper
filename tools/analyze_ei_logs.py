#!/usr/bin/env python3
"""Build a per-phase HTCM mechanic timeline from Elite Insights JSON logs.

Usage:
    # EI JSON from dps.report (public): https://dps.report/getJson?permalink=<id>
    python3 tools/analyze_ei_logs.py data/htcm_timeline.json log1.json log2.json ...

For every phase, events are timed relative to the phase start. Dragon phases
start when the Dragonvoid becomes attackable, purifications when the orb spawns:
both are things the addon can see live. Events from all logs are merged and
clustered (a gap of more than CLUSTER_GAP_MS starts a new cluster), so each
cluster is "this happens at about +Xs into the phase" with its spread across logs.

Event sources:
  CAST   enemy skill casts (target rotations)
  MECH   EI mechanics. Baits/debuffs/greens fire every time (good timers); "hit"
         mechanics only fire when someone fails, so their times are noisier.
  GREEN  green circle spawns (EI's green agents appear; later greens are
         inferred from success/fail time minus GREEN_RESOLVE_MS)

No player names are written to the output.
"""
import json
import os
import statistics
import sys

CLUSTER_GAP_MS = 2500
GREEN_RESOLVE_MS = 6300  # green spawn -> success/fail, constant in every log checked
MECH_DEDUPE_MS = 1500    # same mechanic on several players at once = one event

# Mechanics that are about players, not the encounter's schedule.
GENERIC_MECHS = {
    "Dead", "Downed", "Got up", "Res", "DC", "Knck.Pll", "Lnch", "Lckt",
    "Miss.Qck", "Miss.Alac", "Debilitated", "Infirmity", "Orb Push",
    "NopeRopes.Achiv.L", "NopeRopes.Achiv.K",
}
# Mechanics that happen on schedule regardless of player performance.
SCHEDULED_MECHS = {"Spread.B", "Red.B", "Void.D", "S.Green", "F.Green", "ShckWv.Start"}

PHASES = [
    "Purification 1", "Jormag", "Primordus", "Kralkatorrik",
    "Purification 2", "Void Time Caster", "Mordremoth", "Giants", "Zhaitan",
    "Purification 3", "Void Saltspray Dragon", "Soo-Won 1",
    "Void Obliterator", "Void Goliath", "Soo-Won 2", "Purification 4",
]

out_path = sys.argv[1]
files = sys.argv[2:]


def skill_name(d, sid):
    return d["skillMap"].get(f"s{sid}", {}).get("name", "?")


def events_for_phase(d, ph):
    """(time_ms_relative, key, label, kind) for one phase of one log."""
    s, e = ph["start"], ph["end"]
    ev = []
    for t in d["targets"]:
        if t["id"] <= -110:  # EI's synthetic green agents
            if s <= t["firstAware"] <= e:
                ev.append((t["firstAware"] - s, "GREEN", "Green circles appear", "GREEN"))
            continue
        for r in t.get("rotation") or []:
            if r["id"] < 0:  # weapon swap / dodge pseudo-skills
                continue
            for c in r["skills"]:
                if s - 3000 <= c["castTime"] <= e:
                    who = t["name"].replace("The ", "").replace("Void", "") if t["id"] < 0 else t["name"]
                    ev.append((c["castTime"] - s, f"CAST {r['id']}",
                               f"{who}: {skill_name(d, r['id'])} ({r['id']})" + (f", {c['duration']/1000:.1f}s cast" if c["duration"] else ""),
                               "CAST"))
    for m in d["mechanics"]:
        name = m["name"]
        if name in GENERIC_MECHS:
            continue
        last = -10**12
        for x in sorted(m["mechanicsData"], key=lambda x: x["time"]):
            if not (s - 3000 <= x["time"] <= e) or x["time"] - last <= MECH_DEDUPE_MS:
                continue
            last = x["time"]
            kind = "SCHEDULED" if name in SCHEDULED_MECHS else "HIT"
            ev.append((x["time"] - s, f"MECH {name}", f"{name}: {m['description']}", kind))
            if name in ("S.Green", "F.Green"):
                ev.append((x["time"] - GREEN_RESOLVE_MS - s, "GREEN", "Green circles appear", "GREEN"))
    # Green agents only give the first spawn; inferred spawns can duplicate it.
    ev.sort()
    dedup = []
    for item in ev:
        if item[1] == "GREEN" and any(o[1] == "GREEN" and abs(o[0] - item[0]) < 1500 for o in dedup):
            continue
        dedup.append(item)
    return dedup


logs = []
for f in files:
    with open(f) as fh:
        d = json.load(fh)
    logs.append((os.path.basename(f), d))

result = {
    "source": "Elite Insights JSON (dps.report getJson)",
    "logs": [{"file": n, "cm": d.get("isCM"), "success": d.get("success"), "duration_s": d["durationMS"] / 1000,
              "gw2_build": d.get("gW2Build"), "ei_version": d.get("eliteInsightsVersion")} for n, d in logs],
    "params": {"cluster_gap_ms": CLUSTER_GAP_MS, "green_resolve_ms": GREEN_RESOLVE_MS},
    "phases": {},
}

for pname in PHASES:
    per_log = []
    durations, complete = [], 0
    for n, d in logs:
        ph = next((p for p in d["phases"] if p["name"] == pname), None)
        if not ph:
            continue
        # A phase that runs to the end of a failed log was cut short by the wipe.
        cut = (not d.get("success")) and ph["end"] >= d["durationMS"] - 2000
        if not cut:
            complete += 1
            durations.append((ph["end"] - ph["start"]) / 1000)
        per_log.append((n, events_for_phase(d, ph)))
    if not per_log:
        continue

    by_key = {}
    for n, evs in per_log:
        for t, key, label, kind in evs:
            by_key.setdefault(key, {"label": label, "kind": kind, "points": []})["points"].append((t, n))

    clusters = []
    for key, info in by_key.items():
        pts = sorted(info["points"])
        group = [pts[0]]
        for p in pts[1:]:
            if p[0] - group[-1][0] > CLUSTER_GAP_MS:
                clusters.append((key, info, group))
                group = []
            group.append(p)
        clusters.append((key, info, group))

    rows = []
    for key, info, group in clusters:
        times = [t / 1000 for t, _ in group]
        rows.append({
            "key": key,
            "label": info["label"],
            "kind": info["kind"],
            "median_s": round(statistics.median(times), 1),
            "min_s": round(min(times), 1),
            "max_s": round(max(times), 1),
            "logs": len({n for _, n in group}),
        })
    rows.sort(key=lambda r: (r["median_s"], r["key"]))
    result["phases"][pname] = {
        "logs_with_phase": len(per_log),
        "logs_complete": complete,
        "duration_s": {"min": min(durations), "median": statistics.median(durations), "max": max(durations)} if durations else None,
        "events": rows,
    }

with open(out_path, "w") as fh:
    json.dump(result, fh, indent=1)

for pname, p in result["phases"].items():
    dur = p["duration_s"]
    print(f"\n== {pname}: in {p['logs_with_phase']} logs, {p['logs_complete']} complete"
          + (f", {dur['min']:.0f}-{dur['max']:.0f}s (median {dur['median']:.0f})" if dur else ""))
    for r in p["events"]:
        if r["kind"] == "HIT" and r["logs"] < 3:
            continue  # one-off failures: noise
        spread = f"{r['min_s']:6.1f}..{r['max_s']:<6.1f}"
        print(f"  +{r['median_s']:6.1f}s  [{spread}] {r['logs']:2d}/{p['logs_with_phase']}  {r['kind']:9s} {r['label']}")

# ---- Markdown version (drops one-off noise) ----
md_path = os.path.splitext(out_path)[0] + ".md"
kind_label = {"CAST": "cast", "SCHEDULED": "bait/debuff", "GREEN": "green", "HIT": "someone hit"}
with open(md_path, "w") as fh:
    fh.write("# HTCM mechanic timeline\n\n")
    fh.write(f"Generated by `tools/analyze_ei_logs.py` from {len(logs)} Elite Insights logs "
             f"({sum(1 for _, d in logs if d.get('isCM'))} CM, {sum(1 for _, d in logs if d.get('success'))} kills; "
             f"GW2 build(s) {', '.join(sorted({str(d.get('gW2Build')) for _, d in logs}))}).\n\n")
    fh.write("Times are seconds from the phase start: dragon phases start when the Dragonvoid becomes attackable, "
             "purifications when the orb spawns. **Spread** is min..max across logs. **Seen** is how many logs "
             "that reached this phase had the event; late events are missing from logs where the phase ended "
             "(or the squad wiped) sooner. \"Someone hit\" rows only fire when a player fails the mechanic, so "
             "they're shown only when seen in 3+ logs.\n")
    for pname, p in result["phases"].items():
        dur = p["duration_s"]
        fh.write(f"\n## {pname}\n\n")
        fh.write(f"In {p['logs_with_phase']} logs ({p['logs_complete']} ran to the end of the phase)"
                 + (f"; length {dur['min']:.0f}–{dur['max']:.0f}s, median {dur['median']:.0f}s" if dur else "") + ".\n\n")
        fh.write("| Time | Spread | Seen | Type | Event |\n|---|---|---|---|---|\n")
        for r in p["events"]:
            if r["logs"] < 2 or (r["kind"] == "HIT" and r["logs"] < 3):
                continue
            fh.write(f"| +{r['median_s']:.1f} | {r['min_s']:.1f}..{r['max_s']:.1f} | {r['logs']}/{p['logs_with_phase']} "
                     f"| {kind_label[r['kind']]} | {r['label']} |\n")
print(f"\n-> {out_path}, {md_path}")
