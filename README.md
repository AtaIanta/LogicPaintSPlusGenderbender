# Hatsune Miku Logic Paint S+ - Utility & Customization Hook

A DirectX 11 / Mono game hook (`winmm.dll`) for *Hatsune Miku Logic Paint S+* providing in-game visual customization, puzzle solvers, and bonus stage unlocks.

---

## Features

### In-Game Overlay (`F2`)
- Press **F2** anytime to toggle the configuration menu.
- Non-intrusive DirectX 11 interface with automatic mouse and keyboard input management.

### Visual Customizations (`Visuals`)
- **Puzzle Background & Theme**:
  - Live selection of board skin colors and background illustrations (*Default*, *Miku*, *Rin*, *Len*, *Luka*, *MEIKO*, *KAITO*, *Special*).
  - Automatically updates active puzzles and persists across sessions in `mods/theme_override.ini`.
- **Costume Gender Overrides**:
  - Customize male and female head and body sprite variants for all Crypton characters.
  - Automatically saved to `mods/costume_overrides.ini`.

### Optional Helper Tools (`Cheats`)
Helper tools are disabled by default and configured via `mods/cheats.ini`:

```ini
IamDIRTYlittleCHEATERandDONTwantTOplayTHISgame=0
IamNOTwaitingFORcryptonTOaddGAMEStoSTEAMandWANTtoUNLOCKpuzzlesNOW=0
```

- When both options are `0`, the `Cheats` tab is hidden from the interface.
- Setting either option to `1` reveals the `Cheats` menu with the corresponding actions:
  - **Native Click Auto-Solver** (`IamDIRTYlittleCHEATERandDONTwantTOplayTHISgame=1`):
    - Solves puzzles cell-by-cell using native click calls (`PS_Stage::_onPaintCursor`).
    - Maintains 0 hints used to preserve the 3-Star "NO HINT" completion condition.
  - **Bonus Stage Unlock** (`IamNOTwaitingFORcryptonTOaddGAMEStoSTEAMandWANTtoUNLOCKpuzzlesNOW=1`):
    - Unlocks Tamagotori (`SP35`) and Miku Jigsaw (`SP36`) stages and gallery illustrations directly into save data.
- Configuration is reloaded automatically whenever the overlay is toggled with **F2**.

---

## Installation

1. Download `winmm.dll` from the latest release.
2. Place `winmm.dll` into the game directory next to `MikuLogiS+.exe`.
3. Launch the game through Steam.
4. Press **F2** to open the overlay.

---

## Building from Source

Requires **MinGW-w64** (GCC 64-bit) and **Make**.

```bash
make all
```

Builds `winmm.dll` directly to the output directory.

---

## Project Structure

```text
├── .github/workflows/build.yml   # CI/CD workflow
├── deps/imgui/                   # Dear ImGui library (DX11 + Win32 backend)
├── src/
│   ├── imgui_hook.cpp            # DX11 Present hook & UI rendering
│   ├── overlay.h                 # Shared definitions and declarations
│   ├── patcher.cpp               # Mono reflection, VEH hooks, game logic
│   └── winmm_proxy.cpp           # 64-bit WinMM export forwarder
├── winmm.def                     # Export definitions
├── Makefile                      # Build configuration
└── README.md
```

---

## License

[MIT](LICENSE.md)
