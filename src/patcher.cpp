#include "overlay.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <windows.h>

struct HookData {
  void *target;
  BYTE originalByte;
  const char *funcName;
};
extern __thread HookData *t_SingleStepHook;
extern __thread char t_PendingHead[128];
extern __thread char t_PendingBody[128];

// Mono API types
typedef void *MonoDomain;
typedef void *MonoAssembly;
typedef void *MonoImage;
typedef void *MonoClass;
typedef void *MonoMethod;
typedef void *MonoObject;
typedef void *MonoString;
typedef void *MonoClassField;
typedef void *MonoVTable;
typedef void *MonoArray;
typedef void *MonoJitInfo;

typedef MonoDomain *(*mono_get_root_domain_t)();
typedef void *(*mono_thread_attach_t)(MonoDomain *domain);
typedef MonoAssembly *(*mono_domain_assembly_open_t)(MonoDomain *domain,
                                                     const char *name);
typedef MonoImage *(*mono_assembly_get_image_t)(MonoAssembly *assembly);
typedef MonoClass *(*mono_class_from_name_t)(MonoImage *image,
                                             const char *name_space,
                                             const char *name);
typedef void *(*mono_compile_method_t)(MonoMethod *method);
typedef char *(*mono_string_to_utf8_t)(MonoString *s);
typedef MonoMethod *(*mono_class_get_methods_t)(MonoClass *klass, void **iter);
typedef MonoVTable *(*mono_class_vtable_t)(MonoDomain *domain,
                                           MonoClass *klass);
typedef void *(*mono_vtable_get_static_field_data_t)(MonoVTable *vt);
typedef MonoClassField *(*mono_class_get_field_from_name_t)(MonoClass *klass,
                                                            const char *name);
typedef size_t (*mono_field_get_offset_t)(MonoClassField *field);
typedef void *(*mono_array_addr_with_size_t)(void *array, int size,
                                             uintptr_t idx);
typedef MonoJitInfo *(*mono_jit_info_table_find_t)(MonoDomain *domain,
                                                   void *addr);
typedef MonoMethod *(*mono_jit_info_get_method_t)(MonoJitInfo *ji);
typedef const char *(*mono_method_get_name_t)(MonoMethod *method);
typedef char *(*mono_method_full_name_t)(MonoMethod *method, int signature);

mono_get_root_domain_t mono_get_root_domain;
mono_thread_attach_t mono_thread_attach;
mono_domain_assembly_open_t mono_domain_assembly_open;
mono_assembly_get_image_t mono_assembly_get_image;
mono_class_from_name_t mono_class_from_name;
mono_compile_method_t mono_compile_method;
mono_string_to_utf8_t mono_string_to_utf8;
mono_class_get_methods_t mono_class_get_methods;
mono_class_vtable_t mono_class_vtable;
mono_vtable_get_static_field_data_t mono_vtable_get_static_field_data;
mono_class_get_field_from_name_t mono_class_get_field_from_name;
mono_field_get_offset_t mono_field_get_offset;
mono_array_addr_with_size_t mono_array_addr_with_size;
mono_jit_info_table_find_t mono_jit_info_table_find;
mono_jit_info_get_method_t mono_jit_info_get_method;
mono_method_get_name_t mono_method_get_name;
mono_method_full_name_t mono_method_full_name;

extern bool g_ConsoleEnabled;
extern bool g_DisableCrashHandler;

extern void Log(const char *fmt, ...);

// Character type mapping from E_CHARA enum:
// NONE=0, MIKU=1, RIN=2, LEN=3, LUKA=4, MEIKO=5, KAITO=6, MAX=7
const char *GetCharacterName(int type) {
  switch (type) {
  case 0:
    return "None";
  case 1:
    return "Miku";
  case 2:
    return "Rin";
  case 3:
    return "Len";
  case 4:
    return "Luka";
  case 5:
    return "Meiko";
  case 6:
    return "Kaito";
  case 7:
    return "MAX";
  default: {
    static char typeBuf[32];
    snprintf(typeBuf, sizeof(typeBuf), "Type%d", type);
    return typeBuf;
  }
  }
}

// Costume gender: 0=Male, 1=Female, 2=Unisex
const char *GetGenderName(int gender) {
  switch (gender) {
  case 0:
    return "Male";
  case 1:
    return "Female";
  case 2:
    return "Unisex";
  default:
    return "Unknown";
  }
}

// Pre-cached costume gender lookup (loaded from extracted text files, safe to
// read in VEH)
#define MAX_COSTUMES 512
struct CostumeCacheEntry {
  char id[64];
  int gender;
};
static CostumeCacheEntry g_CostumeCache[MAX_COSTUMES];
static int g_CostumeCacheCount = 0;

void LoadCostumeTableFromFile(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  char line[512];
  int lineNum = 0;
  while (fgets(line, sizeof(line), f)) {
    lineNum++;
    if (lineNum <= 2)
      continue; // skip header + type row
    // Format: CosID,Gender,...
    char cosId[64];
    int gender;
    if (sscanf(line, "%63[^,],%d", cosId, &gender) == 2) {
      if (g_CostumeCacheCount < MAX_COSTUMES) {
        strncpy(g_CostumeCache[g_CostumeCacheCount].id, cosId, 63);
        g_CostumeCache[g_CostumeCacheCount].id[63] = 0;
        g_CostumeCache[g_CostumeCacheCount].gender = gender;
        g_CostumeCacheCount++;
      }
    }
  }
  fclose(f);
}

int LookupCostumeGender(const char *cosId) {
  if (!cosId || cosId[0] == 0)
    return -1;

  // Hardcoded fallbacks for base characters (both Head and Body)
  if (strstr(cosId, "Miku")) return 1;  // Includes SnowMiku, SakuraMiku
  if (strstr(cosId, "Rin")) return 1;
  if (strstr(cosId, "Len")) return 0;
  if (strstr(cosId, "Luka")) return 1;
  if (strstr(cosId, "Meiko")) return 1;
  if (strstr(cosId, "Kaito")) return 0;

  for (int i = 0; i < g_CostumeCacheCount; i++) {
    if (strcmp(g_CostumeCache[i].id, cosId) == 0) {
      return g_CostumeCache[i].gender;
    }
  }
  return -1; // not found
}

// Safe inline wchar->ascii for MonoString without calling mono APIs
// MonoString layout: [vtable:8][lock:8][length:4][firstchar...]
// So chars start at offset 0x14 from the MonoString pointer
static bool SafeReadMonoString(void *monoStr, char *buf, int bufSize) {
  if (!monoStr || IsBadReadPtr(monoStr, 0x18)) {
    buf[0] = 0;
    return false;
  }
  int len = *(int *)((char *)monoStr + 0x10);
  if (len <= 0 || len > bufSize - 1) {
    if (len > bufSize - 1)
      len = bufSize - 1;
    if (len <= 0) {
      buf[0] = 0;
      return false;
    }
  }
  wchar_t *wchars = (wchar_t *)((char *)monoStr + 0x14);
  if (IsBadReadPtr(wchars, len * 2)) {
    buf[0] = 0;
    return false;
  }
  for (int i = 0; i < len; i++) {
    buf[i] = (char)(wchars[i] & 0x7F); // ASCII only
  }
  buf[len] = 0;
  return true;
}

// In-place mutation of the trailing gender digit in a Mono string (e.g.
// "Head_022_1" -> "Head_022_0")
static void MutateMonoStringGender(void *monoStr, int overrideGender) {
  if (!monoStr || IsBadReadPtr(monoStr, 0x18))
    return;
  int len = *(int *)((char *)monoStr + 0x10);
  if (len < 3)
    return; // Need string like "X_0"
  wchar_t *chars = (wchar_t *)((char *)monoStr + 0x14);
  if (IsBadReadPtr(chars, len * 2))
    return;
  if (chars[len - 2] == L'_') {
    wchar_t current = chars[len - 1];
    if (current == L'0' || current == L'1' || current == L'2') {
      chars[len - 1] = (overrideGender == 0) ? L'0' : L'1';
    }
  }
}

// VEH Trap Logic
struct XRayTrap {
  std::vector<HookData> hooks;
  MonoDomain *domain;
  MonoImage *image;
  // Thread-safe single-stepping
};
__thread HookData *t_SingleStepHook = nullptr;
__thread char t_PendingHead[128] = "";
__thread char t_PendingBody[128] = "";
XRayTrap g_XRay = {{}, NULL, NULL};

static const char *g_BakedIgnoreList[] = {
    "Body_000",         "Body_001",          "Body_002",
    "Body_003",         "Body_004",          "Body_005",
    "Body_031",         "Body_032",          "Body_033",
    "Body_034",         "Body_035",          "Body_061",
    "Body_062",         "Body_063",          "Body_064",
    "Body_065",         "Body_091",          "Body_092",
    "Body_093",         "Body_094",          "Body_095",
    "Body_121",         "Body_122",          "Body_123",
    "Body_124",         "Body_125",          "Body_151",
    "Body_152",         "Body_153",          "Body_154",
    "Body_155",         "Head_001",          "Head_002",         "Head_003",          "Head_004",
    "Head_005",         "Head_006",          "Head_007",
    "Head_008",         "Head_009",          "Head_010",
    "Head_011",         "Head_012",          "Head_013",
    "Head_014",         "Head_015",          "Head_016",
    "Head_017",         "Head_018",          "Head_019",
    "Head_030",         "Head_031",          "Head_032",
    "Head_033",         "Head_034",          "Head_035",
    "Head_036",         "Head_037",          "Head_038",
    "Head_039",         "Head_040",          "Head_041",
    "Head_042",         "Head_043",          "Head_044",
    "Head_045",         "Head_046",          "Head_047",
    "Head_048",         "Head_049",          "Head_050",
    "Head_060",         "Head_061",          "Head_062",
    "Head_063",         "Head_064",          "Head_065",
    "Head_066",         "Head_067",          "Head_068",
    "Head_069",         "Head_070",          "Head_071",
    "Head_072",         "Head_073",          "Head_074",
    "Head_075",         "Head_076",          "Head_077",
    "Head_078",         "Head_079",          "Head_080",
    "Head_090",         "Head_091",          "Head_092",
    "Head_093",         "Head_094",          "Head_095",
    "Head_096",         "Head_097",          "Head_098",
    "Head_099",         "Head_100",          "Head_101",
    "Head_102",         "Head_103",          "Head_104",
    "Head_105",         "Head_106",          "Head_107",
    "Head_108",         "Head_109",          "Head_110",
    "Head_120",         "Head_121",          "Head_122",
    "Head_123",         "Head_124",          "Head_125",
    "Head_126",         "Head_127",          "Head_128",
    "Head_129",         "Head_130",          "Head_131",
    "Head_132",         "Head_133",          "Head_134",
    "Head_135",         "Head_136",          "Head_137",
    "Head_138",         "Head_139",          "Head_140",
    "Head_150",         "Head_151",          "Head_152",
    "Head_153",         "Head_154",          "Head_155",
    "Head_156",         "Head_157",          "Head_158",
    "Head_159",         "Head_160",          "Head_161",
    "Head_162",         "Head_163",          "Head_164",
    "Head_165",         "Head_166",          "Head_167",
    "Head_168",         "Head_169",          "Head_170",
    "Head_180",         "Head_Rin",
    "Head_000",         "BodyParts"};

bool IsSafeToOverride(const char *costumeId) {
  if (!costumeId || costumeId[0] == '\0')
    return false;

  // Forbidden keywords: Base characters and DLC assets that lack gender variants
  static const char *forbidden[] = {"DLC",   "Miku",  "Luka",  "Rin",
                                    "Len",   "Meiko", "Kaito", "BodyParts",
                                    "HeadItem"};
  for (const char *name : forbidden) {
    if (strstr(costumeId, name) != nullptr) {
      return false;
    }
  }

  for (const char *baked : g_BakedIgnoreList) {
    if (strstr(costumeId, baked) != nullptr)
      return false;
  }

  return true;
}

LONG WINAPI XRayHandler(PEXCEPTION_POINTERS ep) {
  // Handle our breakpoints
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_BREAKPOINT) {
    HookData *activeHook = nullptr;
    for (auto &h : g_XRay.hooks) {
      if (h.target == ep->ExceptionRecord->ExceptionAddress) {
        activeHook = &h;
        break;
      }
    }

    if (activeHook) {
      if (strcmp(activeHook->funcName, "CreateProcessW") == 0) {
        const wchar_t *appName = (const wchar_t *)ep->ContextRecord->Rcx;
        const wchar_t *cmdLine = (const wchar_t *)ep->ContextRecord->Rdx;
        bool block = false;
        if (appName && wcsstr(appName, L"UnityCrashHandler"))
          block = true;
        if (cmdLine && wcsstr(cmdLine, L"UnityCrashHandler"))
          block = true;

        if (block) {
          Log("[Patcher] Blocked UnityCrashHandler launch (W).\n");
          ep->ContextRecord->Rip = *(uintptr_t *)ep->ContextRecord->Rsp;
          ep->ContextRecord->Rsp += 8;
          ep->ContextRecord->Rax = TRUE;
          return EXCEPTION_CONTINUE_EXECUTION;
        }
      } else if (strcmp(activeHook->funcName, "CreateProcessA") == 0) {
        const char *appName = (const char *)ep->ContextRecord->Rcx;
        const char *cmdLine = (const char *)ep->ContextRecord->Rdx;
        bool block = false;
        if (appName && strstr(appName, "UnityCrashHandler"))
          block = true;
        if (cmdLine && strstr(cmdLine, "UnityCrashHandler"))
          block = true;

        if (block) {
          Log("[Patcher] Blocked UnityCrashHandler launch (A).\n");
          ep->ContextRecord->Rip = *(uintptr_t *)ep->ContextRecord->Rsp;
          ep->ContextRecord->Rsp += 8;
          ep->ContextRecord->Rax = TRUE;
          return EXCEPTION_CONTINUE_EXECUTION;
        }
      } else if (strcmp(activeHook->funcName, "WinExec") == 0) {
        const char *cmdLine = (const char *)ep->ContextRecord->Rcx;
        if (cmdLine && strstr(cmdLine, "UnityCrashHandler")) {
          Log("[Patcher] Blocked UnityCrashHandler launch (WinExec).\n");
          ep->ContextRecord->Rip = *(uintptr_t *)ep->ContextRecord->Rsp;
          ep->ContextRecord->Rsp += 8;
          ep->ContextRecord->Rax =
              31; // Greater than 31 means success for WinExec
          return EXCEPTION_CONTINUE_EXECUTION;
        }
      }

      void *instance = (void *)ep->ContextRecord->Rcx;
      char charNameBuf[128] = "Unknown";
      int gender = -1, type = -1;

      if (instance && !IsBadReadPtr(instance, 0x50)) {
        void *msName = *(void **)((char *)instance + 0x48);
        SafeReadMonoString(msName, charNameBuf, sizeof(charNameBuf));
        gender = *(int *)((char *)instance + 0x44);
        type = *(int *)((char *)instance + 0x40);
      }

      if (strcmp(activeHook->funcName, "changeCostume") == 0) {
        // Read register args for changeCostume
        void *headId = (void *)ep->ContextRecord->Rdx;
        void *bodyId = (void *)ep->ContextRecord->R8;
        char headBuf[128] = "None";
        char bodyBuf[128] = "None";
        SafeReadMonoString(headId, headBuf, sizeof(headBuf));
        SafeReadMonoString(bodyId, bodyBuf, sizeof(bodyBuf));

        // Capture for TLS setup hooks
        strncpy(t_PendingHead, headBuf, sizeof(t_PendingHead));
        strncpy(t_PendingBody, bodyBuf, sizeof(t_PendingBody));

        // Caller identification via return address
        char callerBuf[256] = "UnknownCaller";
        void *retAddr = *(void **)ep->ContextRecord->Rsp;
        if (g_XRay.domain && mono_jit_info_table_find &&
            mono_jit_info_get_method && mono_method_get_name) {
          MonoJitInfo *ji = mono_jit_info_table_find(g_XRay.domain, retAddr);
          if (ji) {
            MonoMethod *method = mono_jit_info_get_method(ji);
            if (method) {
              const char *mname = mono_method_get_name(method);
              if (mname) {
                strncpy(callerBuf, mname, sizeof(callerBuf) - 1);
                callerBuf[sizeof(callerBuf) - 1] = 0;
              }
            }
          }
        }

        int headGender = LookupCostumeGender(headBuf);
        int bodyGender = LookupCostumeGender(bodyBuf);

        // Log everything to file
        Log("\n--- Costume Change Hook ---\n");
        Log("Trigger: %s\n", callerBuf);
        Log("Character: %s (%s) [Type:%d, Gender:%d(%s)]\n", charNameBuf,
            GetCharacterName(type), type, gender, GetGenderName(gender));
        Log("Costume -> Head: %s [%s], Body: %s [%s]\n", headBuf,
            headGender >= 0 ? GetGenderName(headGender) : "?", bodyBuf,
            bodyGender >= 0 ? GetGenderName(bodyGender) : "?");

        if (type >= 0 && type < CHARA_COUNT) {
          if (g_Overrides[type].headGender >= 0)
            Log("  [OVERRIDE PENDING] Head gender -> %s\n",
                GetGenderName(g_Overrides[type].headGender));
          if (g_Overrides[type].bodyGender >= 0)
            Log("  [OVERRIDE PENDING] Body gender -> %s\n",
                GetGenderName(g_Overrides[type].bodyGender));
        }
        Log("---------------------------\n");
      } else if (strcmp(activeHook->funcName, "_loadAndSetHeadSpLibAsset") ==
                     0 ||
                 strcmp(activeHook->funcName, "_setHead") == 0) {
        if (type >= 0 && type < CHARA_COUNT &&
            g_Overrides[type].headGender >= 0) {
          if (IsSafeToOverride(t_PendingHead)) {
            if (instance && !IsBadReadPtr(instance, 0x50)) {
              *(int *)((char *)instance + 0x44) = g_Overrides[type].headGender;
            }
            if (strcmp(activeHook->funcName, "_loadAndSetHeadSpLibAsset") ==
                0) {
              void *assetStr = (void *)ep->ContextRecord->Rdx;
              char assetName[128] = "";
              SafeReadMonoString(assetStr, assetName, sizeof(assetName));

              if (strchr(assetName, '_')) {
                MutateMonoStringGender(assetStr, g_Overrides[type].headGender);
                Log("  [Applied] %s mutation to gender %d for: %s (Context: "
                    "%s)\n",
                    activeHook->funcName, g_Overrides[type].headGender,
                    assetName, t_PendingHead);
              } else {
                Log("  [Applied] %s property only (Asset: %s, Context: %s)\n",
                    activeHook->funcName, assetName, t_PendingHead);
              }
            }
          } else {
            // FORCE DEFAULT GENDER for unsafe context to prevent crash from previous part setup
            int defaultGender = LookupCostumeGender(t_PendingHead);
            if (defaultGender >= 0 && instance && !IsBadReadPtr(instance, 0x50)) {
              *(int *)((char *)instance + 0x44) = defaultGender;
            }

            static std::string lastHeadLog = "";
            if (lastHeadLog != t_PendingHead) {
              if (defaultGender >= 0) {
                Log("  [Safety] Forced default gender %d for %s (ignored "
                    "costume)\n",
                    defaultGender, t_PendingHead);
              } else {
                Log("  [Safety] Skipped override for unknown ID %s (No "
                    "Force)\n", t_PendingHead);
              }
              lastHeadLog = t_PendingHead;
            }
          }
        }
      } else if (strcmp(activeHook->funcName, "_loadAndSetBodySpLibAsset") ==
                     0 ||
                 strcmp(activeHook->funcName, "_setBody") == 0) {
        if (type >= 0 && type < CHARA_COUNT &&
            g_Overrides[type].bodyGender >= 0) {
          if (IsSafeToOverride(t_PendingBody)) {
            if (instance && !IsBadReadPtr(instance, 0x50)) {
              *(int *)((char *)instance + 0x44) = g_Overrides[type].bodyGender;
            }
            if (strcmp(activeHook->funcName, "_loadAndSetBodySpLibAsset") ==
                0) {
              void *assetStr = (void *)ep->ContextRecord->Rdx;
              char assetName[128] = "";
              SafeReadMonoString(assetStr, assetName, sizeof(assetName));

              if (strchr(assetName, '_')) {
                MutateMonoStringGender(assetStr, g_Overrides[type].bodyGender);
                Log("  [Applied] %s mutation to gender %d for: %s (Context: "
                    "%s)\n",
                    activeHook->funcName, g_Overrides[type].bodyGender,
                    assetName, t_PendingBody);
              } else {
                Log("  [Applied] %s property only (Asset: %s, Context: %s)\n",
                    activeHook->funcName, assetName, t_PendingBody);
              }
            }
          } else {
            // FORCE DEFAULT GENDER for unsafe context to prevent crash from previous part setup
            int defaultGender = LookupCostumeGender(t_PendingBody);
            if (defaultGender >= 0 && instance && !IsBadReadPtr(instance, 0x50)) {
              *(int *)((char *)instance + 0x44) = defaultGender;
            }

            static std::string lastBodyLog = "";
            if (lastBodyLog != t_PendingBody) {
              if (defaultGender >= 0) {
                Log("  [Safety] Forced default gender %d for %s (ignored "
                    "costume)\n",
                    defaultGender, t_PendingBody);
              } else {
                Log("  [Safety] Skipped override for unknown ID %s (No "
                    "Force)\n", t_PendingBody);
              }
              lastBodyLog = t_PendingBody;
            }
          }
        }
      }

      // RESTORE EXECUTION: let the original function run
      DWORD oldProt;
      VirtualProtect(activeHook->target, 1, PAGE_EXECUTE_READWRITE, &oldProt);
      *(BYTE *)activeHook->target = activeHook->originalByte;
      VirtualProtect(activeHook->target, 1, oldProt, &oldProt);

      t_SingleStepHook = activeHook;
      ep->ContextRecord->EFlags |= 0x100; // Trap Flag
      return EXCEPTION_CONTINUE_EXECUTION;
    }
  }

  // Handle single-step ONLY if it's ours
  if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_SINGLE_STEP &&
      t_SingleStepHook) {
    HookData *activeHook = t_SingleStepHook;
    t_SingleStepHook = nullptr;

    // Re-install breakpoint
    DWORD oldProt;
    VirtualProtect(activeHook->target, 1, PAGE_EXECUTE_READWRITE, &oldProt);
    *(BYTE *)activeHook->target = 0xCC;
    VirtualProtect(activeHook->target, 1, oldProt, &oldProt);

    return EXCEPTION_CONTINUE_EXECUTION;
  }

  return EXCEPTION_CONTINUE_SEARCH;
}

void StartPatching() {
  Log("[Patcher] X-Ray Diagnostic Boot (v39 - Stable)...\n");
  Sleep(10000);

  HMODULE hMono = GetModuleHandleA("mono-2.0-bdwgc.dll");
  while (!hMono) {
    Sleep(100);
    hMono = GetModuleHandleA("mono-2.0-bdwgc.dll");
  }

  auto GetMonoFunc = [&](const char *name) -> void * {
    return (void *)GetProcAddress(hMono, name);
  };
  mono_get_root_domain =
      (mono_get_root_domain_t)GetMonoFunc("mono_get_root_domain");
  mono_thread_attach = (mono_thread_attach_t)GetMonoFunc("mono_thread_attach");
  mono_domain_assembly_open =
      (mono_domain_assembly_open_t)GetMonoFunc("mono_domain_assembly_open");
  mono_assembly_get_image =
      (mono_assembly_get_image_t)GetMonoFunc("mono_assembly_get_image");
  mono_class_from_name =
      (mono_class_from_name_t)GetMonoFunc("mono_class_from_name");
  mono_compile_method =
      (mono_compile_method_t)GetMonoFunc("mono_compile_method");
  mono_string_to_utf8 =
      (mono_string_to_utf8_t)GetMonoFunc("mono_string_to_utf8");
  mono_class_get_methods =
      (mono_class_get_methods_t)GetMonoFunc("mono_class_get_methods");
  mono_class_vtable = (mono_class_vtable_t)GetMonoFunc("mono_class_vtable");
  mono_vtable_get_static_field_data =
      (mono_vtable_get_static_field_data_t)GetMonoFunc(
          "mono_vtable_get_static_field_data");
  mono_class_get_field_from_name =
      (mono_class_get_field_from_name_t)GetMonoFunc(
          "mono_class_get_field_from_name");
  mono_field_get_offset =
      (mono_field_get_offset_t)GetMonoFunc("mono_field_get_offset");
  mono_array_addr_with_size =
      (mono_array_addr_with_size_t)GetMonoFunc("mono_array_addr_with_size");
  mono_jit_info_table_find =
      (mono_jit_info_table_find_t)GetMonoFunc("mono_jit_info_table_find");
  mono_jit_info_get_method =
      (mono_jit_info_get_method_t)GetMonoFunc("mono_jit_info_get_method");
  mono_method_get_name =
      (mono_method_get_name_t)GetMonoFunc("mono_method_get_name");
  mono_method_full_name =
      (mono_method_full_name_t)GetMonoFunc("mono_method_full_name");

  MonoDomain *domain = mono_get_root_domain();
  if (!domain)
    return;
  mono_thread_attach(domain);
  g_XRay.domain = domain;

  MonoAssembly *assembly = mono_domain_assembly_open(
      domain, "MikuLogiS+_Data/Managed/Assembly-CSharp.dll");
  if (!assembly)
    return;
  MonoImage *image = mono_assembly_get_image(assembly);
  g_XRay.image = image;

  MonoClass *costumeClass = mono_class_from_name(image, "", "PS_Costume");
  if (costumeClass) {
    AddVectoredExceptionHandler(1, XRayHandler);

    const char *targetMethods[] = {"changeCostume", "_setHead", "_setBody",
                                   "_loadAndSetHeadSpLibAsset",
                                   "_loadAndSetBodySpLibAsset"};
    void *iter = NULL;
    MonoMethod *m;
    while ((m = mono_class_get_methods(costumeClass, &iter))) {
      const char *mname = mono_method_get_name(m);
      if (mname) {
        for (const char *t : targetMethods) {
          if (strcmp(mname, t) == 0) {
            void *target = mono_compile_method(m);

            HookData hook;
            hook.target = target;
            hook.originalByte = *(BYTE *)target;
            hook.funcName = t;
            g_XRay.hooks.push_back(hook);

            DWORD old;
            if (VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old)) {
              *(BYTE *)target = 0xCC;
              VirtualProtect(target, 1, old, &old);
              Log("[Patcher] X-Ray Trap Active: %s @ %p\n", t, target);
            }
            break;
          }
        }
      }
    }
  }

  // Load costume gender data from extracted text files
  LoadCostumeTableFromFile("extracted/tables/CostumeTable.txt");
  LoadCostumeTableFromFile("extracted/tables/CostumeTable_DLC_01.txt");
  Log("[Patcher] Loaded %d costume entries from text files.\n",
      g_CostumeCacheCount);


  extern bool g_DisableCrashHandler;
  if (g_DisableCrashHandler) {
    const char *k32hooks[] = {"CreateProcessW", "CreateProcessA", "WinExec"};
    HMODULE hK32 = GetModuleHandleA("kernel32.dll");
    for (const char *name : k32hooks) {
      void *target = (void *)GetProcAddress(hK32, name);
      if (target) {
        HookData hook;
        hook.target = target;
        hook.originalByte = *(BYTE *)target;
        hook.funcName = name;
        g_XRay.hooks.push_back(hook);
        DWORD old;
        VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &old);
        *(BYTE *)target = 0xCC;
        VirtualProtect(target, 1, old, &old);
        Log("[Patcher] %s Hook Active (Crash Handler Bypass)\n", name);
      }
    }
  }

  // Dump E_CHARA enum to discover correct character type mapping
  typedef MonoClass *(*mono_class_get_nested_types_t)(MonoClass *klass,
                                                      void **iter);
  typedef const char *(*mono_class_get_name_t)(MonoClass *klass);
  typedef MonoClassField *(*mono_class_get_fields_t)(MonoClass *klass,
                                                     void **iter);
  typedef const char *(*mono_field_get_name_t)(MonoClassField *field);
  typedef void (*mono_field_static_get_value_t)(
      MonoVTable *vt, MonoClassField *field, void *value);

  auto p_mono_class_get_nested_types =
      (mono_class_get_nested_types_t)GetMonoFunc("mono_class_get_nested_types");
  auto p_mono_class_get_name =
      (mono_class_get_name_t)GetMonoFunc("mono_class_get_name");
  auto p_mono_class_get_fields =
      (mono_class_get_fields_t)GetMonoFunc("mono_class_get_fields");
  auto p_mono_field_get_name =
      (mono_field_get_name_t)GetMonoFunc("mono_field_get_name");
  auto p_mono_field_static_get_value =
      (mono_field_static_get_value_t)GetMonoFunc("mono_field_static_get_value");

  MonoClass *gameDefClass = mono_class_from_name(image, "", "GameDef");
  if (gameDefClass && p_mono_class_get_nested_types && p_mono_class_get_name) {
    void *nIter = NULL;
    MonoClass *nested;
    while ((nested = p_mono_class_get_nested_types(gameDefClass, &nIter))) {
      const char *nName = p_mono_class_get_name(nested);
      if (nName && strcmp(nName, "E_CHARA") == 0) {
        Log("[Patcher] === E_CHARA Enum Dump ===\n");
        MonoVTable *enumVt = mono_class_vtable(domain, nested);
        void *fIter = NULL;
        MonoClassField *field;
        while ((field = p_mono_class_get_fields(nested, &fIter))) {
          const char *fname = p_mono_field_get_name(field);
          if (fname && strcmp(fname, "value__") != 0) {
            int val = -999;
            if (enumVt && p_mono_field_static_get_value) {
              p_mono_field_static_get_value(enumVt, field, &val);
            }
            Log("  E_CHARA.%s = %d\n", fname, val);
          }
        }
        Log("[Patcher] === End E_CHARA ===\n");
        break;
      }
    }
  } else {
    Log("[Patcher] GameDef class not found for E_CHARA dump.\n");
  }
  // Initialize ImGui overlay for gender overrides
  InitImGuiHook();

  Log("[Patcher] --- Clothing Patcher Initialized ---\n");
}
