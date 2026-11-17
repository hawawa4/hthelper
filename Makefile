# Cross-compile the addon DLL from Linux with MinGW-w64.
#   sudo apt install g++-mingw-w64-x86-64-posix
#   make
CXX      := x86_64-w64-mingw32-g++-posix
# Or point at llvm-mingw: make CXX=/path/to/llvm-mingw/bin/x86_64-w64-mingw32-clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Ithird_party/imgui -DWIN32_LEAN_AND_MEAN -DNOMINMAX
LDFLAGS  := -shared -static -static-libgcc -static-libstdc++ -Wl,--subsystem,windows
LDLIBS   := -lwinmm

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

build/obj/%.o: %.cpp src/Nexus.h src/Recorder.h src/font_anton.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf build

.PHONY: all clean
