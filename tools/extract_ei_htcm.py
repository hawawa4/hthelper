#!/usr/bin/env python3
"""Extract Harvest Temple IDs and mechanics from the Elite Insights source.

Usage:
    git clone --depth 1 --filter=blob:none --sparse \\
        https://github.com/baaron4/GW2-Elite-Insights-Parser.git ei
    git -C ei sparse-checkout set --no-cone '*HarvestTemple*' '*SpeciesIDs*' \\
        '*SkillIDs*' '*EffectGUIDs*'
    python3 tools/extract_ei_htcm.py ei data/htcm_ei.json

Writes every skill/buff ID, species ID and effect GUID that EI's HarvestTemple.cs
references, plus its mechanic list grouped by fight phase. Negative species IDs
are EI-internal (EI re-labels agents after parsing); see data/htcm.md.
"""
import json
import re
import subprocess
import sys

EI_ROOT = sys.argv[1]
OUT = sys.argv[2]
P = EI_ROOT + "/GW2EI.Library/GW2EI.Services/GW2EIEvtcParser/"
HT_PATH = P + "LogLogic/Raids/RaidEncounters/EndOfDragons/Bosses/HarvestTemple.cs"


def read(path):
    with open(path, encoding="utf-8-sig") as f:
        return f.read()


ht = read(HT_PATH)
skills = {n: int(v) for n, v in re.findall(r"public const long (\w+) = (-?\d+);", read(P + "ParserHelpers/IDs/SkillIDs.cs"))}

sp_src = read(P + "ParserHelpers/IDs/SpeciesIDs.cs")
private_species = {n: int(v) for n, v in re.findall(r"private const int (\w+) = (-?\d+);", sp_src)}
species = {}
for name, val in re.findall(r"^\s*(\w+) = ([\w.\-]+),", sp_src, re.M):
    if val.lstrip("-").isdigit():
        species.setdefault(name, int(val))
    elif val.startswith("SpeciesIDs."):
        species.setdefault(name, private_species.get(val.split(".")[1]))

effects = dict(re.findall(r'public static readonly Guid (\w+) = new\("([0-9A-F]+)"\)', read(P + "ParserHelpers/GUIDs/EffectGUIDs.cs")))

# ---- mechanic list, grouped by the "// Phase" comments in the constructor ----
start = ht.index("MechanicList.Add(")
end = ht.index("Icon = EncounterIconHarvestTemple")
block = ht[start:end]

mechanics = []
group, subgroup = None, None
pending = ""
for raw in block.splitlines():
    line = raw.strip()
    m = re.match(r"^// (.+)$", line)
    if m:
        name = m.group(1).strip()
        # Top-level phase comments are indented 12 spaces, sub-groups 16.
        if len(raw) - len(raw.lstrip()) <= 12:
            group, subgroup = name, None
        else:
            subgroup = name
        continue
    if line.startswith("new ") and "Mechanic(" in line:
        pending = line
    elif pending and not line.startswith("new MechanicGroup"):
        pending += " " + line
    if not pending:
        continue
    if not re.search(r'new\("[^"]*", "[^"]*", "[^"]*"\)', pending):
        continue
    kind = re.match(r"new (\w+)\(", pending).group(1)
    args = pending[len(kind) + 5:]
    if args.startswith("["):
        id_tokens = re.findall(r"[\w.]+", args[1:args.index("]")])
    else:
        id_tokens = [re.match(r"[\w.]+", args).group(0)]
    short, desc, name = re.search(r'new\("([^"]*)", "([^"]*)", "([^"]*)"\)', pending).groups()
    ids = []
    for tok in id_tokens:
        if tok.startswith("EffectGUIDs."):
            t = tok.split(".", 1)[1]
            ids.append({"effect": t, "guid": effects.get(t)})
        elif tok in skills:
            ids.append({"skill": tok, "id": skills[tok]})
        elif tok.startswith("Mech_"):
            pass
        else:
            ids.append({"other": tok})
    mechanics.append({
        "phase": group,
        "subgroup": subgroup,
        "kind": kind,
        "short": short,
        "name": name,
        "description": desc,
        "ids": ids,
    })
    pending = ""

# Only look at code: words inside strings/comments ("Barrage", "Fury") would
# otherwise match unrelated skill constants.
code = re.sub(r'"(?:[^"\\]|\\.)*"|//[^\n]*', "", ht)
referenced = lambda pat: sorted(set(re.findall(pat, code)))
try:
    ei_commit = subprocess.run(["git", "-C", EI_ROOT, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
except OSError:
    ei_commit = None

out = {
    "source": {
        "repo": "https://github.com/baaron4/GW2-Elite-Insights-Parser",
        "commit": ei_commit,
        "file": HT_PATH[len(EI_ROOT) + 1:],
    },
    "skills": {t: skills[t] for t in referenced(r"\b([A-Z]\w+)\b") if t in skills},
    "species": {t: species.get(t) for t in referenced(r"TargetID\.(\w+)") if t != "Unknown"},
    "effects": {t: effects.get(t) for t in referenced(r"EffectGUIDs\.(\w+)") if t != "Any"},
    "mechanics": mechanics,
}
with open(OUT, "w") as f:
    json.dump(out, f, indent=1)
print(f"{len(out['skills'])} skills, {len(out['species'])} species, {len(out['effects'])} effects, {len(mechanics)} mechanics -> {OUT}")
