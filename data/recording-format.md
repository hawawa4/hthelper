# Recording format

Recordings are CSV files in `<GW2>/addons/HTCMHelper/recordings/`, one per instance, named `map<id>_<date>_<time>_<n>.csv`. Pulls are marked by `9:SQCOMBATSTART` / `10:SQCOMBATEND` state changes.

| kind | what | notes |
|---|---|---|
| `AGENT_ADD` / `AGENT_REMOVE` | an agent entered or left tracking | for NPCs, `src_prof` is the species ID |
| `TARGET_CHANGE` | you changed target | |
| `CAST` | NPC/gadget skill activation (`START`, `CANCEL_*`, ...) | `value` = expected cast time in ms |
| `STATECHANGE` | everything except position/velocity/facing | `sub` = `code:NAME` |
| `BUFF_APPLY` / `BUFF_REMOVE` | buffs involving an NPC, or landing on you | `value` = duration in ms |
| `DMG_SEC` | squad damage to one NPC, summed per second | `value` = damage, `buff_dmg` = hit count |
| `HIT_SELF` | an enemy hit on you (`DIRECT` or `CONDI`) | `result`: 0 normal, 1 crit, 3 block, 4 evade, 5 interrupt, 6 invuln, 7 blind |

- `time` is ArcDPS's millisecond clock.
- `stream` is the Nexus event the row came from: `local` or `squad`.
- The last four columns (`ev_src_agent`, `ev_dst_agent`, `overstack`, `pad`) are raw ArcDPS fields. Some state changes keep their data there. For example, `46:IDTOGUID` packs an effect GUID into `ev_src_agent` + `ev_dst_agent`, which is how the AoEs in `htcm_ei.json` are identified.
