#!/usr/bin/env python3
"""Generate src/EncounterData.h (the addon's HTCM callout table) from data/.

Usage: python3 tools/gen_encounter_data.py data/htcm_timeline.json data/htcm_ei.json src/EncounterData.h

Each callout is an event from the timeline at a fixed offset into its phase.
Callouts that map to something ArcDPS sends live (a missile, an effect GUID,
a buff, an NPC cast) carry a trigger; when that trigger fires, the addon
re-syncs the phase clock to it. Mapping from Elite Insights' synthesized casts
to live events follows HarvestTemple.cs (SpecialCastEventProcess): EI's cast
time equals the missile/effect time.
"""
import json
import sys

timeline_path, ei_path, out_path = sys.argv[1:4]
timeline = json.load(open(timeline_path))
ei = json.load(open(ei_path))
effects = ei["effects"]

# Phases the engine tracks, in fight order. (EI's "Void Time Caster" and
# "Giants" overlap other phases; their attacks are reactive alerts instead.)
PHASES = [
    ("Purification 1", "P1"), ("Jormag", "Jormag"), ("Primordus", "Primordus"),
    ("Kralkatorrik", "Kralkatorrik"), ("Purification 2", "P2"), ("Mordremoth", "Mordremoth"),
    ("Zhaitan", "Zhaitan"), ("Purification 3", "P3"), ("Soo-Won 1", "SooWon1"),
    ("Soo-Won 2", "SooWon2"), ("Purification 4", "P4"),
]
# Default phase length if the timeline has none (no logs reached it yet).
DEFAULT_MAX_S = 120

# timeline key -> (label, style, triggers)
# style: 0 = list only, 1 = big alert before it happens
# trigger: ("missile", skill) | ("effect", effect_name) | ("buff", buff) | ("cast", skill)
CALLOUTS = {
    "GREEN":        ("Green", 1, [("effect", "HarvestTempleGreen")]),
    "MECH Spread.B": ("Spread", 1, [("effect", "HarvestTempleTargetedExpulsionSpreadCM"), ("effect", "HarvestTempleTargetedExpulsionSpreadNM")]),
    "MECH Red.B":   ("Red puddles", 1, [("effect", "HarvestTempleVoidPoolRedPuddleSelectionCM"), ("effect", "HarvestTempleVoidPoolRedPuddleSelectionNM")]),
    # Jormag
    "CAST 66216":   ("Breath (South)", 0, [("missile", 66216)]),
    "CAST 67607":   ("Breath (Center)", 0, []),   # EI derives this from a moving beam agent
    "CAST 65517":   ("Breath (North)", 0, []),    # same
    "CAST 67772":   ("Grasp", 0, [("missile", 67772)]),
    # Primordus
    "CAST 65704":   ("Jaws", 1, [("effect", "HarvestTemplePrimordusJawsOfDestructionIndicator")]),
    "CAST 65427":   ("Lava Slam", 0, [("effect", "HarvestTemplePrimordusLavaSlamHitIndicator")]),
    # Kralkatorrik
    "CAST 65017":   ("Branding Beam", 0, [("effect", "HarvestTempleKralkatorrikBeamIndicator")]),
    "CAST 66790":   ("Crystal Barrage", 0, [("missile", 66790)]),
    # Mordremoth
    "CAST 64576":   ("Poison Roar", 0, [("missile", 64576)]),
    "CAST 64810":   ("Shock Wave", 1, [("effect", "HarvestTempleMordremothShockwave1")]),
    # Zhaitan
    "CAST 64932":   ("Tail Slam", 0, [("effect", "HarvestTempleZhaitanTailSlamImpact")]),
    "CAST 64428":   ("Scream", 0, [("effect", "HarvestTempleZhaitanScreamIndicator")]),
    "CAST 65132":   ("Putrid Deluge", 0, [("missile", 65132)]),
}

# NPC casts that are real activations (not scheduled): show a big alert while
# they're being cast. (skill id, label, cast ms fallback)
REACTIVE = [
    (66886, "DEATH SCREAM - stability!", 7600),
    (63767, "STOMP - stability!", 4300),
    (66059, "NIGHTMARE EPOCH", 1100),
]

# Live phase-entry signals.
ORB_SPECIES = 24375          # Void Amalgamate (purification orb)
FINAL_ORB_SPECIES = 23956    # Killable Void Amalgamate (Purification 4)
DRAGONVOID_GADGETS = [43488, 1378]

# Keep events seen in at least this share of the logs that reached the phase
# (late events are naturally rarer because phases end at different times).
MIN_SHARE = 0.3


def guid_bytes(hexstr):
    return ", ".join(f"0x{hexstr[i:i+2]}" for i in range(0, 32, 2))


triggers = []  # (kind, id_or_guid, label)
trigger_index = {}


def trig(t):
    key = (t[0], t[1])
    if key not in trigger_index:
        trigger_index[key] = len(triggers)
        triggers.append(key)
    return trigger_index[key]


phase_rows = []
for ename, cname in PHASES:
    p = timeline["phases"].get(ename)
    rows = []
    max_s = DEFAULT_MAX_S
    if p:
        if p["duration_s"]:
            max_s = p["duration_s"]["max"]
        n = p["logs_with_phase"]
        for ev in p["events"]:
            if ev["key"] not in CALLOUTS or ev["median_s"] < 0:
                continue
            if ev["logs"] < max(2, MIN_SHARE * n) and ev["logs"] < n:
                continue
            label, style, trigs = CALLOUTS[ev["key"]]
            tids = [trig(t) for t in trigs]
            rows.append((int(round(ev["median_s"] * 1000)), label, style, tids,
                         int(round((ev["max_s"] - ev["min_s"]) * 1000))))
    rows.sort()
    phase_rows.append((ename, cname, int(max_s * 1000), rows))

# Which triggers appear in only one phase (those can start the phase).
trig_phases = {}
for i, (_, cname, _, rows) in enumerate(phase_rows):
    for r in rows:
        for t in r[3]:
            trig_phases.setdefault(t, set()).add(i)

with open(out_path, "w") as f:
    w = f.write
    w("// GENERATED by tools/gen_encounter_data.py from data/htcm_timeline.json and\n")
    w("// data/htcm_ei.json. Don't edit by hand; edit the script and regenerate.\n")
    w("#pragma once\n\n#include <cstdint>\n\nnamespace EncounterData\n{\n")
    w("\tenum class ETrigger : uint8_t { Missile, Effect };\n\n")
    w("\tstruct Trigger\n\t{\n\t\tETrigger    Type;\n\t\tuint32_t    SkillID;   // Missile\n\t\tuint8_t     Guid[16];  // Effect\n\t\tconst char* Name;\n\t\tint8_t      OnlyPhase; // phase index if it appears in exactly one phase, else -1\n\t};\n\n")
    w("\tstruct Callout\n\t{\n\t\tint32_t     OffsetMs;\n\t\tconst char* Label;\n\t\tuint8_t     BigAlert;\n\t\tint16_t     Triggers[2]; // -1 = none\n\t\tint32_t     SpreadMs;    // min..max spread across logs\n\t};\n\n")
    w("\tstruct Phase\n\t{\n\t\tconst char*    Name;\n\t\tbool           IsDragon;\n\t\tint32_t        MaxDurationMs;\n\t\tconst Callout* Callouts;\n\t\tint            CalloutCount;\n\t};\n\n")
    w("\tstruct Reactive\n\t{\n\t\tuint32_t    SkillID;\n\t\tconst char* Label;\n\t\tint32_t     DefaultCastMs;\n\t};\n\n")

    w("\tinline const Trigger TRIGGERS[] = {\n")
    for i, (kind, val) in enumerate(triggers):
        only = trig_phases.get(i, set())
        only_idx = next(iter(only)) if len(only) == 1 else -1
        if kind == "missile":
            w(f"\t\t{{ ETrigger::Missile, {val}, {{}}, \"missile {val}\", {only_idx} }},\n")
        else:
            g = effects[val]
            w(f"\t\t{{ ETrigger::Effect, 0, {{ {guid_bytes(g)} }}, \"{val}\", {only_idx} }},\n")
    w("\t};\n")
    w(f"\tconstexpr int TRIGGER_COUNT = {len(triggers)};\n\n")

    for ename, cname, max_ms, rows in phase_rows:
        if not rows:
            continue
        w(f"\tinline const Callout CALLOUTS_{cname}[] = {{\n")
        for off, label, style, tids, spread in rows:
            t = (tids + [-1, -1])[:2]
            w(f"\t\t{{ {off:6d}, \"{label}\", {style}, {{ {t[0]}, {t[1]} }}, {spread} }},\n")
        w("\t};\n")
    w("\n\tinline const Phase PHASES[] = {\n")
    for ename, cname, max_ms, rows in phase_rows:
        dragon = "false" if cname.startswith("P") and cname[1:].isdigit() else "true"
        if rows:
            w(f"\t\t{{ \"{ename}\", {dragon}, {max_ms}, CALLOUTS_{cname}, {len(rows)} }},\n")
        else:
            w(f"\t\t{{ \"{ename}\", {dragon}, {max_ms}, nullptr, 0 }},\n")
    w("\t};\n")
    w(f"\tconstexpr int PHASE_COUNT = {len(phase_rows)};\n\n")

    w("\tinline const Reactive REACTIVES[] = {\n")
    for sid, label, ms in REACTIVE:
        w(f"\t\t{{ {sid}, \"{label}\", {ms} }},\n")
    w("\t};\n")
    w(f"\tconstexpr int REACTIVE_COUNT = {len(REACTIVE)};\n\n")
    w(f"\tconstexpr uint32_t ORB_SPECIES       = {ORB_SPECIES};\n")
    w(f"\tconstexpr uint32_t FINAL_ORB_SPECIES = {FINAL_ORB_SPECIES};\n")
    w(f"\tconstexpr uint32_t DRAGONVOID_GADGETS[] = {{ {', '.join(map(str, DRAGONVOID_GADGETS))} }};\n")
    w("}\n")

print(f"{len(triggers)} triggers, {sum(len(r[3]) for r in phase_rows)} callouts in "
      f"{sum(1 for r in phase_rows if r[3])}/{len(phase_rows)} phases -> {out_path}")
for ename, _, max_ms, rows in phase_rows:
    print(f"  {ename:16s} max {max_ms/1000:5.0f}s  " + ", ".join(f"{o/1000:.1f} {l}" for o, l, *_ in rows))
