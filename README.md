# HTCM Helper (Nexus addon)

A Guild Wars 2 [Nexus](https://github.com/RaidcoreGG/Nexus) addon for the Harvest Temple CM strike.

**v0.1:** when your character dies, the screen flashes **LEAVE THE INSTANCE NOW** in big red letters.

## How it works
- **Death detection:** listens for ArcDPS combat events, which Nexus forwards as `EV_ARCDPS_COMBATEVENT_LOCAL_RAW`. The overlay turns on when your character's state changes to dead, and turns off when you're revived or respawn. **ArcDPS must be installed.**
- **Map:** reads MumbleLink (`DL_MUMBLE_LINK`). By default the overlay only shows in Harvest Temple (map 1437), and it clears when you change maps.
- **Options** (Nexus → Addons → HTCM Helper): turn off the map filter, preview the overlay, see the current map ID.

## Recording
While you're in Harvest Temple, the addon writes ArcDPS combat events to CSV files in `<GW2>/addons/HTCMHelper/recordings/`. It starts a new file for each instance, named `map<id>_<date>_<time>_<n>.csv`. Pulls are marked inside the file by `9:SQCOMBATSTART` / `10:SQCOMBATEND` state changes.

| kind | what | notes |
|---|---|---|
| `AGENT_ADD` / `AGENT_REMOVE` | an agent entered or left tracking | for NPCs, `src_prof` is the **species ID** |
| `TARGET_CHANGE` | you changed target | |
| `CAST` | NPC/gadget skill activation (`START`, `CANCEL_*`, ...) | `skill_id`, `skill_name`; `value` = expected cast time in ms |
| `STATECHANGE` | everything except position/velocity/facing | `sub` = `code:NAME` |
| `BUFF_APPLY` / `BUFF_REMOVE` | buffs involving an NPC, or landing on you | `value` = duration in ms |
| `DMG_SEC` | squad (players + their pets) damage to one NPC, summed per second | `value` = damage, `buff_dmg` = number of hits |

`time` is ArcDPS's millisecond clock. `stream` says which Nexus event the row came from (`local` = `EV_ARCDPS_COMBATEVENT_LOCAL_RAW`, `squad` = `..._SQUAD_RAW`). Turn recording off, or on for every map, in the addon options.

## Build
Linux (cross-compile):
```sh
sudo apt install g++-mingw-w64-x86-64-posix
make            # -> build/htcm_helper.dll
```
Windows (MSVC): `cmake -B build -A x64 && cmake --build build --config Release`

## Install
Copy `htcm_helper.dll` into `<GW2>/addons/`, then enable it in the Nexus menu (Ctrl+O).

## Layout
- `src/main.cpp`: the addon
- `src/Nexus.h`: a minimal copy of the Nexus API v6 structs, plus the ArcDPS and MumbleLink structs it needs
- `third_party/imgui`: ImGui 1.80, the version Nexus uses (MIT)
- `third_party/fonts`: the [Anton](https://github.com/googlefonts/AntonFont) font (SIL OFL 1.1), cut down to printable ASCII. It's embedded as `src/font_anton.h` (regenerate with `xxd -i -n AntonAscii_ttf`). The addon registers it with Nexus at the pixel size your resolution needs, so the text is drawn at 1:1 instead of stretched.
