#include "Window.h"
#include <windowsx.h>

#ifdef EDITOR_BUILD
#include "imgui.h"
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif

namespace {

const char* g_ClassName = "4x11EngineWnd";
HWND g_Hwnd = nullptr;
int g_Width = 1280;
int g_Height = 720;
bool g_Resized = false;
int g_NewW = 0;
int g_NewH = 0;

bool g_Keys[256] = {};
int  g_MouseX = 0, g_MouseY = 0;
bool g_MouseBtns[3] = {};

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l)
{
#ifdef EDITOR_BUILD
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, w, l))
        return true;
#endif

    switch (msg) {
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    case WM_SIZE: {
        int nw = LOWORD(l), nh = HIWORD(l);
        if (nw > 0 && nh > 0 && (nw != g_Width || nh != g_Height)) {
            g_NewW = nw; g_NewH = nh; g_Resized = true;
        }
        return 0;
    }
    case WM_KEYDOWN:
        if (w < 256) g_Keys[w] = true;
        return 0;
    case WM_KEYUP:
        if (w < 256) g_Keys[w] = false;
        return 0;
    case WM_MOUSEMOVE:
        g_MouseX = GET_X_LPARAM(l);
        g_MouseY = GET_Y_LPARAM(l);
        return 0;
    case WM_LBUTTONDOWN: g_MouseBtns[0] = true;  return 0;
    case WM_LBUTTONUP:   g_MouseBtns[0] = false; return 0;
    case WM_RBUTTONDOWN: g_MouseBtns[1] = true;  return 0;
    case WM_RBUTTONUP:   g_MouseBtns[1] = false; return 0;
    case WM_MBUTTONDOWN: g_MouseBtns[2] = true;  return 0;
    case WM_MBUTTONUP:   g_MouseBtns[2] = false; return 0;
    }
    return DefWindowProcA(hwnd, msg, w, l);
}

} // anonymous namespace

namespace Window {

bool Create(HINSTANCE hInst, int width, int height, const char* title)
{
    g_Width  = width;
    g_Height = height;

    WNDCLASSA wc = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = g_ClassName;
    RegisterClassA(&wc);

    g_Hwnd = CreateWindowA(g_ClassName, title,
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           g_Width, g_Height,
                           nullptr, nullptr, hInst, nullptr);
    if (!g_Hwnd) return false;

    ShowWindow(g_Hwnd, SW_SHOW);
    UpdateWindow(g_Hwnd);
    return true;
}

bool ProcessMessages()
{
    MSG msg = {};
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    if (g_Resized) {
        g_Width  = g_NewW;
        g_Height = g_NewH;
    }
    return true;
}

void ClearResized() { g_Resized = false; }

void Shutdown()
{
    DestroyWindow(g_Hwnd);
    g_Hwnd = nullptr;
    UnregisterClassA(g_ClassName, GetModuleHandleA(nullptr));
}

HWND Handle() { return g_Hwnd; }
int Width()   { return g_Width; }
int Height()  { return g_Height; }
bool Resized(){ return g_Resized; }

bool* Keys()          { return g_Keys; }
int   MouseX()        { return g_MouseX; }
int   MouseY()        { return g_MouseY; }
bool  MouseDown(int btn) { return (btn >= 0 && btn < 3) ? g_MouseBtns[btn] : false; }

} // namespace Window
