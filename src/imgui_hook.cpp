#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>
#include "overlay.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

extern void Log(const char* fmt, ...);

// --- Globals ---
GenderOverride g_Overrides[CHARA_COUNT] = {};
bool g_ShowOverlay = false;

static ID3D11Device*            g_pDevice = nullptr;
static ID3D11DeviceContext*     g_pContext = nullptr;
static ID3D11RenderTargetView*  g_pRTV = nullptr;
static HWND                     g_hWnd = nullptr;
static WNDPROC                  g_OrigWndProc = nullptr;
static bool                     g_ImGuiInit = false;

// Present hook
typedef HRESULT(WINAPI* PresentFn)(IDXGISwapChain* sc, UINT syncInterval, UINT flags);
static PresentFn g_OrigPresent = nullptr;
static void* g_PresentTarget = nullptr; // Address we patched

// Forward decl
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// --- Persistence ---
void LoadOverrides() {
    for (int i = 0; i < CHARA_COUNT; i++) {
        g_Overrides[i].headGender = -1;
        g_Overrides[i].bodyGender = -1;
    }
    CreateDirectoryA("mods", NULL);
    FILE* f = fopen("mods/costume_overrides.ini", "r");
    if (!f) return;
    int idx, head, body;
    while (fscanf(f, "%d %d %d", &idx, &head, &body) == 3) {
        if (idx >= 0 && idx < CHARA_COUNT) {
            g_Overrides[idx].headGender = head;
            g_Overrides[idx].bodyGender = body;
        }
    }
    fclose(f);
    Log("[Overlay] Loaded overrides from mods/costume_overrides.ini\n");
}

void SaveOverrides() {
    CreateDirectoryA("mods", NULL);
    FILE* f = fopen("mods/costume_overrides.ini", "w");
    if (!f) return;
    for (int i = 0; i < CHARA_COUNT; i++) {
        fprintf(f, "%d %d %d\n", i, g_Overrides[i].headGender, g_Overrides[i].bodyGender);
    }
    fclose(f);
}

// --- WndProc Hook ---
static LRESULT CALLBACK HookedWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN && wParam == VK_F2) {
        g_ShowOverlay = !g_ShowOverlay;
        return 0;
    }
    if (g_ShowOverlay && g_ImGuiInit) {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return 0;

        ImGuiIO& io = ImGui::GetIO();
        // Block mouse input to the game ONLY if ImGui is capturing it (hovering/clicking windows)
        if (io.WantCaptureMouse && msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST)
            return 0;
        // Block keyboard input to the game ONLY if ImGui is capturing it (typing in text boxes)
        if (io.WantCaptureKeyboard && msg >= WM_KEYFIRST && msg <= WM_KEYLAST && wParam != VK_F2)
            return 0;
    }
    return CallWindowProcA(g_OrigWndProc, hWnd, msg, wParam, lParam);
}

// --- Overlay Rendering ---
static void RenderOverlay() {
    if (!g_ShowOverlay) return;

    ImGui::SetNextWindowSize(ImVec2(420, 300), ImGuiCond_FirstUseEver);
    ImGui::Begin("Costume Gender Override [F2]", &g_ShowOverlay);

    static const char* charaNames[] = {"None", "Miku", "Rin", "Len", "Luka", "Meiko", "Kaito"};
    static const char* genderOpts[] = {"Default", "Male", "Female"};

    ImGui::Text("Override gender variant for head/body sprites:");
    ImGui::Separator();

    ImGui::Columns(3, "overrides");
    ImGui::SetColumnWidth(0, 80);
    ImGui::SetColumnWidth(1, 160);
    ImGui::Text("Character"); ImGui::NextColumn();
    ImGui::Text("Head"); ImGui::NextColumn();
    ImGui::Text("Body"); ImGui::NextColumn();
    ImGui::Separator();

    bool changed = false;
    for (int i = 1; i < CHARA_COUNT; i++) { // skip NONE (0)
        ImGui::Text("%s", charaNames[i]);
        ImGui::NextColumn();

        // Head override: -1=Default, 0=Male, 1=Female -> combo index 0,1,2
        int headSel = g_Overrides[i].headGender + 1;
        ImGui::PushID(i * 2);
        if (ImGui::Combo("##h", &headSel, genderOpts, 3)) {
            g_Overrides[i].headGender = headSel - 1;
            changed = true;
        }
        ImGui::PopID();
        ImGui::NextColumn();

        // Body override
        int bodySel = g_Overrides[i].bodyGender + 1;
        ImGui::PushID(i * 2 + 1);
        if (ImGui::Combo("##b", &bodySel, genderOpts, 3)) {
            g_Overrides[i].bodyGender = bodySel - 1;
            changed = true;
        }
        ImGui::PopID();
        ImGui::NextColumn();
    }
    ImGui::Columns(1);

    ImGui::Separator();
    if (ImGui::Button("Reset All")) {
        for (int i = 0; i < CHARA_COUNT; i++) {
            g_Overrides[i].headGender = -1;
            g_Overrides[i].bodyGender = -1;
        }
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        SaveOverrides();
    }

    if (changed) {
        SaveOverrides();
    }

    ImGui::End();
}

// --- Present Hook ---
static HRESULT WINAPI HookedPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
    if (!g_ImGuiInit) {
        // Get device and context
        if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pDevice))) {
            g_pDevice->GetImmediateContext(&g_pContext);

            // Get window handle
            DXGI_SWAP_CHAIN_DESC desc;
            pSwapChain->GetDesc(&desc);
            g_hWnd = desc.OutputWindow;

            // Create render target view
            ID3D11Texture2D* pBack = nullptr;
            pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBack);
            if (pBack) {
                g_pDevice->CreateRenderTargetView(pBack, nullptr, &g_pRTV);
                pBack->Release();
            }

            // Init ImGui
            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            ImGui::StyleColorsDark();

            // Make it look nicer
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 6.0f;
            style.FrameRounding = 4.0f;
            style.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.08f, 0.12f, 0.92f);
            style.Colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.05f, 0.25f, 1.0f);
            style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.25f, 0.10f, 0.40f, 1.0f);

            ImGui_ImplWin32_Init(g_hWnd);
            ImGui_ImplDX11_Init(g_pDevice, g_pContext);

            // Hook WndProc
            g_OrigWndProc = (WNDPROC)SetWindowLongPtrA(g_hWnd, GWLP_WNDPROC, (LONG_PTR)HookedWndProc);

            g_ImGuiInit = true;
            Log("[Overlay] ImGui initialized on DX11 SwapChain\n");
        }
    }

    if (g_ImGuiInit) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        RenderOverlay();

        ImGui::Render();
        g_pContext->OMSetRenderTargets(1, &g_pRTV, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    return g_OrigPresent(pSwapChain, SyncInterval, Flags);
}

// --- VMT Hook Helper ---
static void* HookVTableFunc(void* pVTable, int index, void* pHook) {
    void** vtable = *(void***)pVTable;
    void* pOrig = vtable[index];
    DWORD oldProt;
    VirtualProtect(&vtable[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt);
    vtable[index] = pHook;
    VirtualProtect(&vtable[index], sizeof(void*), oldProt, &oldProt);
    return pOrig;
}

// --- Init ---
void InitImGuiHook() {
    LoadOverrides();

    // Create dummy device to find SwapChain vtable
    WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0, 0,
                       GetModuleHandleA(NULL), NULL, NULL, NULL, NULL, "DX11Dummy", NULL };
    RegisterClassExA(&wc);
    HWND hDummy = CreateWindowA("DX11Dummy", "", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hDummy;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* pDummySC = nullptr;
    ID3D11Device* pDummyDev = nullptr;
    ID3D11DeviceContext* pDummyCtx = nullptr;

    D3D_FEATURE_LEVEL featureLevel;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0,
        D3D11_SDK_VERSION, &sd, &pDummySC, &pDummyDev, &featureLevel, &pDummyCtx);

    if (FAILED(hr)) {
        Log("[Overlay] FAILED to create dummy DX11 device (hr=0x%08X)\n", hr);
        DestroyWindow(hDummy);
        UnregisterClassA("DX11Dummy", wc.hInstance);
        return;
    }

    // Hook Present (vtable index 8)
    g_OrigPresent = (PresentFn)HookVTableFunc(pDummySC, 8, (void*)HookedPresent);
    Log("[Overlay] Hooked IDXGISwapChain::Present\n");

    pDummySC->Release();
    pDummyDev->Release();
    pDummyCtx->Release();
    DestroyWindow(hDummy);
    UnregisterClassA("DX11Dummy", wc.hInstance);
}
