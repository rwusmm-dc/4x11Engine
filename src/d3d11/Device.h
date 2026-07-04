#pragma once
#include <d3d11.h>
#include <dxgi.h>
#include "core/ComPtr.h"

namespace d3d11 {
namespace Device {

bool Init(HWND hwnd, int width, int height);
void Shutdown();
void Clear(float r, float g, float b, float a);
void Present();
void Resize(int width, int height);
ID3D11Device* GetD3D();
ID3D11DeviceContext* GetCtx();
IDXGISwapChain* GetSwapChain();
ID3D11RenderTargetView* GetRTV();
ID3D11DepthStencilView* GetDSV();

} // namespace Device
} // namespace d3d11