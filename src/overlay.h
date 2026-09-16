#pragma once

// Gender override state per character
// E_CHARA: 0=NONE, 1=MIKU, 2=RIN, 3=LEN, 4=LUKA, 5=MEIKO, 6=KAITO
#define CHARA_COUNT 7

struct GenderOverride {
    int headGender; // -1 = Default (no override), 0 = Male, 1 = Female
    int bodyGender; // -1 = Default (no override), 0 = Male, 1 = Female
};

extern GenderOverride g_Overrides[CHARA_COUNT];
extern bool g_ShowOverlay;

void InitImGuiHook();
void LoadOverrides();
void SaveOverrides();

// Theme override: 0 = Default (Stage's own theme), 1 = Miku, 2 = Rin, 3 = Len, 4 = Luka, 5 = Meiko, 6 = Kaito, 7 = Other
extern int g_ThemeOverride;

void RequestAutoSolve();
void RequestThemeChange(int theme);
void RequestUnlockTamagotoriJigsaw();
bool IsInPuzzle();
bool IsPuzzleCleared();
bool AreTamagotoriJigsawUnlocked();
void LoadThemeConfig();
void SaveThemeConfig();

// --- Cheat Settings Toggles ---
extern bool IamDIRTYlittleCHEATERandDONTwantTOplayTHISgame;
extern bool IamNOTwaitingFORcryptonTOaddGAMEStoSTEAMandWANTtoUNLOCKpuzzlesNOW;

void LoadCheatConfig();
void SaveCheatConfig();
