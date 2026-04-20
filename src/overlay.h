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
