# Cross-compile the addon DLL from Linux with MinGW-w64.
#   sudo apt install g++-mingw-w64-x86-64-posix
#   make
CXX      := x86_64-w64-mingw32-g++-posix
# Or point at llvm-mingw: make CXX=/path/to/llvm-mingw/bin/x86_64-w64-mingw32-clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Ithird_party/imgui -DWIN32_LEAN_AND_MEAN -DNOMINMAX
LDFLAGS  := -shared -static -static-libgcc -static-libstdc++ -Wl,--subsystem,windows
LDLIBS   := -lwinmm

# ImGui 1.80 (the version Nexus uses) is downloaded on first build, not committed.
IMGUI_VERSION := 1.80
IMGUI_URL     := https://github.com/ocornut/imgui/archive/refs/tags/v$(IMGUI_VERSION).tar.gz
IMGUI_SHA256  := d7e4e1c7233409018437a646680316040e6977b9a635c02da93d172baad94ce9
IMGUI_DIR     := third_party/imgui
IMGUI_STAMP   := $(IMGUI_DIR)/.v$(IMGUI_VERSION)

SRC := src/main.cpp \
       src/Recorder.cpp \
       src/Encounter.cpp \
       third_party/imgui/imgui.cpp \
       third_party/imgui/imgui_draw.cpp \
       third_party/imgui/imgui_widgets.cpp \
       third_party/imgui/imgui_tables.cpp

OUT := build/htcm_helper.dll
OBJ := $(patsubst %.cpp,build/obj/%.o,$(SRC))

all: $(OUT)

$(OUT): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/obj/%.o: %.cpp src/Nexus.h src/Recorder.h src/Encounter.h src/EncounterData.h src/font_anton.h | $(IMGUI_STAMP)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# The ImGui sources only exist after this runs.
$(filter $(IMGUI_DIR)/%,$(SRC)): $(IMGUI_STAMP)

$(IMGUI_STAMP):
	@echo "Downloading ImGui $(IMGUI_VERSION)..."
	@rm -rf $(IMGUI_DIR) && mkdir -p $(IMGUI_DIR)
	curl -fsSL -o $(IMGUI_DIR).tar.gz $(IMGUI_URL)
	echo "$(IMGUI_SHA256)  $(IMGUI_DIR).tar.gz" | sha256sum -c --quiet
	tar xzf $(IMGUI_DIR).tar.gz -C $(IMGUI_DIR) --strip-components=1
	@rm -f $(IMGUI_DIR).tar.gz
	@touch $@

# Regenerate the callout table after changing data/ or the generator.
encounter-data:
	python3 tools/gen_encounter_data.py data/htcm_timeline.json data/htcm_ei.json src/EncounterData.h

# Native (Linux) replay test of the encounter engine. Pass EVENTS=file from
# tools/ei_to_events.py.
build/encounter_replay: tests/encounter_replay.cpp src/Encounter.cpp src/Encounter.h src/EncounterData.h src/Nexus.h
	@mkdir -p build
	g++ -std=c++17 -O1 -Wall -Isrc tests/encounter_replay.cpp src/Encounter.cpp -o $@

test: build/encounter_replay
	@if [ -z "$(EVENTS)" ]; then echo "usage: make test EVENTS=events.txt"; exit 2; fi
	build/encounter_replay $(EVENTS)

imgui: $(IMGUI_STAMP)

clean:
	rm -rf build

distclean: clean
	rm -rf $(IMGUI_DIR)

.PHONY: all imgui encounter-data test clean distclean
