#pragma once
#include <windows.h>

enum class GfxBackend { None, D3D10, D3D11 };

namespace Window {

bool Create(HINSTANCE hInst, int width, int height, const char* title);
bool ProcessMessages();
void Shutdown();
HWND Handle();
int Width();
int Height();
bool Resized();
void ClearResized();

// Input
bool* Keys();
int  MouseX();
int  MouseY();
bool MouseDown(int btn); // 0=left, 1=right, 2=middle

} // namespace Window
