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
       third_party/imgui/imgui.cpp \
       third_party/imgui/imgui_draw.cpp \
       third_party/imgui/imgui_widgets.cpp \
       third_party/imgui/imgui_tables.cpp

OUT := build/htcm_helper.dll
OBJ := $(patsubst %.cpp,build/obj/%.o,$(SRC))

all: $(OUT)

$(OUT): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $^ $(LDLIBS)

build/obj/%.o: %.cpp src/Nexus.h src/Recorder.h src/font_anton.h | $(IMGUI_STAMP)
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

imgui: $(IMGUI_STAMP)

clean:
	rm -rf build

distclean: clean
	rm -rf $(IMGUI_DIR)

.PHONY: all imgui clean distclean
