#!/usr/bin/env python3
"""Turn an Elite Insights JSON log into synthetic live events for tests/encounter_replay.

Usage: python3 tools/ei_to_events.py data/htcm_ei.json log.json out.txt [--no-targetable] [--no-orbs]

Inverts EI's HarvestTemple cast synthesis: casts EI built from missiles become
MISSILE events, casts built from effects become EFFECT events (with the GUID),
and EI's bait/green mechanics become their effect GUIDs. Each real event also
gets a PROBE line 3s earlier, so the test can check what the engine predicted.

Line format: <time_ms> <TYPE> <args...>
  ATTACKTARGET <attack_agent>     TARGETABLE <attack_agent> <0|1>
  MISSILE <skill>                 EFFECT <guid_hex>
  ORB <agent_id> <species>        PROBE <label>
"""
import json
import sys

ei_ids, log_path, out_path = sys.argv[1:4]
flags = set(sys.argv[4:])
effects = json.load(open(ei_ids))["effects"]
d = json.load(open(log_path))

PROBE_LEAD_MS = 3000
BASE = 1_000_000  # arbitrary clock offset

# skill id -> (live event, probe label)
CASTS = {
    66216: (("MISSILE", 66216), "Breath (South)"),
    67772: (("MISSILE", 67772), "Grasp"),
    65704: (("EFFECT", "HarvestTemplePrimordusJawsOfDestructionIndicator"), "Jaws"),
    65427: (("EFFECT", "HarvestTemplePrimordusLavaSlamHitIndicator"), "Lava Slam"),
    65017: (("EFFECT", "HarvestTempleKralkatorrikBeamIndicator"), "Branding Beam"),
    66790: (("MISSILE", 66790), "Crystal Barrage"),
    64576: (("MISSILE", 64576), "Poison Roar"),
    64810: (("EFFECT", "HarvestTempleMordremothShockwave1"), "Shock Wave"),
    64932: (("EFFECT", "HarvestTempleZhaitanTailSlamImpact"), "Tail Slam"),
    64428: (("EFFECT", "HarvestTempleZhaitanScreamIndicator"), "Scream"),
    65132: (("MISSILE", 65132), "Putrid Deluge"),
}
MECHS = {
    "Spread.B": (("EFFECT", "HarvestTempleTargetedExpulsionSpreadCM"), "Spread"),
    "Red.B": (("EFFECT", "HarvestTempleVoidPoolRedPuddleSelectionCM"), "Red puddles"),
}

lines = []


def add(t, *parts):
    lines.append((BASE + int(t), " ".join(str(p) for p in parts)))


def live(t, ev):
    kind, val = ev
    add(t, kind, effects[val] if kind == "EFFECT" else val)


DRAGONVOID_ATTACK_AGENT = 777
if "--no-targetable" not in flags:
    add(0, "ATTACKTARGET", DRAGONVOID_ATTACK_AGENT)

for t in d["targets"]:
    tid = t["id"]
    if -22 <= tid <= -17 and "--no-targetable" not in flags:  # dragons (EI labels)
        add(t["firstAware"], "TARGETABLE", DRAGONVOID_ATTACK_AGENT, 1)
        add(t["lastAware"], "TARGETABLE", DRAGONVOID_ATTACK_AGENT, 0)
    if tid in (-23, 23956) and "--no-orbs" not in flags:  # purification orbs
        add(t["firstAware"], "ORB", 5000 + t["instanceID"], 24375 if tid == -23 else 23956)
    for r in t.get("rotation") or []:
        if r["id"] in CASTS:
            ev, label = CASTS[r["id"]]
            for c in r["skills"]:
                live(c["castTime"], ev)
                add(c["castTime"] - PROBE_LEAD_MS, "PROBE", label)

greens = set()
for t in d["targets"]:
    if t["id"] <= -110:
        greens.add(t["firstAware"])
for m in d["mechanics"]:
    if m["name"] in MECHS:
        ev, label = MECHS[m["name"]]
        last = -10**9
        for x in sorted(m["mechanicsData"], key=lambda x: x["time"]):
            if x["time"] - last > 1500:
                live(x["time"], ev)
                add(x["time"] - PROBE_LEAD_MS, "PROBE", label)
            last = x["time"]
    if m["name"] in ("S.Green", "F.Green"):
        for x in m["mechanicsData"]:
            greens.add(x["time"] - 6300)
merged = []
for g in sorted(greens):
    if not merged or g - merged[-1] > 1500:
        merged.append(g)
for g in merged:
    live(g, ("EFFECT", "HarvestTempleGreen"))
    add(g - PROBE_LEAD_MS, "PROBE", "Green")

lines.sort(key=lambda x: (x[0], not x[1].startswith("PROBE")))
with open(out_path, "w") as f:
    for t, s in lines:
        f.write(f"{t} {s}\n")
print(f"{len(lines)} lines -> {out_path}")
