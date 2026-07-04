#pragma once
#include <d3d10.h>
#include <dxgi.h>
#include "core/ComPtr.h"

namespace d3d10 {
namespace Device {

bool Init(HWND hwnd, int width, int height);
void Shutdown();
void Clear(float r, float g, float b, float a);
void Present();
void Resize(int width, int height);
ID3D10Device* GetD3D();
IDXGISwapChain* GetSwapChain();
ID3D10RenderTargetView* GetRTV();
ID3D10DepthStencilView* GetDSV();

} // namespace Device
} // namespace d3d10
