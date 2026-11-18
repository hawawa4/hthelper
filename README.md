# HTCM Helper

A [Nexus](https://github.com/RaidcoreGG/Nexus) addon for the Guild Wars 2 Harvest Temple CM strike. Requires ArcDPS.

## Features

- **Death warning:** when you die, the screen flashes **LEAVE THE INSTANCE NOW**.
- **Mechanic timers:** shows upcoming mechanics with countdowns, and a big alert 3 seconds before green, spread, red puddles and Jaws. Covers Jormag through the start of Zhaitan. Soo-Won isn't done yet because we haven't reached that lmao
- **Recording:** saves combat events to CSV in `<GW2>/addons/HTCMHelper/recordings/`. See [data/recording-format.md](data/recording-format.md). 

Settings are in Nexus → Addons → HTCM Helper.

## Install

Copy `htcm_helper.dll` into `<GW2>/addons/` and enable it in Nexus (Ctrl+O).

## Build

Linux (cross-compile):

```sh
sudo apt install g++-mingw-w64-x86-64-posix
make    # output: build/htcm_helper.dll
```

Windows: `cmake -B build -A x64 && cmake --build build --config Release`

The first build downloads ImGui 1.80.

## Development

- `src/` — addon source
- `data/` — encounter data; start with `data/htcm.md`
- `tools/` — scripts that generate `data/` from Elite Insights logs
- `make encounter-data` — regenerate `src/EncounterData.h` from `data/htcm_timeline.json`
- `make test EVENTS=<file>` — replay events through the timer engine (make the file with `tools/ei_to_events.py`)
