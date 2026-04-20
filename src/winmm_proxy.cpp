#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

bool g_ConsoleEnabled = false;
bool g_DisableCrashHandler = false;

void Log(const char* fmt, ...) {
    if (!g_ConsoleEnabled) return;
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

void StartPatching();

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    switch (fdwReason) {
        case DLL_PROCESS_ATTACH: {
            // Ensure mods folder exists
            CreateDirectoryA("mods", NULL);

            // Check config for console
            char configPath[MAX_PATH];
            GetCurrentDirectoryA(MAX_PATH, configPath);
            strcat(configPath, "\\mods\\patch_config.ini");
            
            // Generate default config if missing
            if (GetFileAttributesA(configPath) == INVALID_FILE_ATTRIBUTES) {
                WritePrivateProfileStringA("Settings", "ShowConsole", "0", configPath);
                WritePrivateProfileStringA("Settings", "DisableCrashHandler", "1", configPath);
            }

            if (GetPrivateProfileIntA("Settings", "DisableCrashHandler", 0, configPath)) {
                g_DisableCrashHandler = true;
            }

            // Self-terminate if we are loaded by the Crash Handler
            if (g_DisableCrashHandler) {
                char procPath[MAX_PATH];
                GetModuleFileNameA(NULL, procPath, MAX_PATH);
                if (strstr(procPath, "UnityCrashHandler64.exe")) {
                    TerminateProcess(GetCurrentProcess(), 0);
                }
            }

            if (GetPrivateProfileIntA("Settings", "ShowConsole", 0, configPath)) {
                AllocConsole();
                freopen("CONOUT$", "w", stdout);
                freopen("CONOUT$", "w", stderr);
                g_ConsoleEnabled = true;
                Log("[Proxy] Console Initialized (v23 Mode)\n");
            }

            HMODULE hSelf;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, 
                               (LPCSTR)DllMain, &hSelf)) {
                // DLL Pinned
            }

            CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)StartPatching, NULL, 0, NULL);
            break;
        }
    }
    return TRUE;
}
