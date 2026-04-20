CC = g++
CFLAGS = -shared -fPIC -m64 -O2 -Isrc -Ideps
LDFLAGS = -static-libgcc -static-libstdc++ -Wl,--subsystem,windows -ld3d11 -ldxgi -ld3dcompiler -lgdi32 -ldwmapi

SRC_DIR = src
IMGUI_DIR = deps/imgui
OUT = winmm.dll

IMGUI_SRC = $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_widgets.cpp \
            $(IMGUI_DIR)/imgui_tables.cpp $(IMGUI_DIR)/imgui_impl_dx11.cpp $(IMGUI_DIR)/imgui_impl_win32.cpp

all: $(OUT)

$(OUT): $(SRC_DIR)/winmm_proxy.cpp $(SRC_DIR)/patcher.cpp $(SRC_DIR)/imgui_hook.cpp winmm.def $(IMGUI_SRC)
	$(CC) $(CFLAGS) -o $(OUT) $(SRC_DIR)/winmm_proxy.cpp $(SRC_DIR)/patcher.cpp $(SRC_DIR)/imgui_hook.cpp $(IMGUI_SRC) winmm.def $(LDFLAGS)

clean:
	del /F /Q $(OUT)
