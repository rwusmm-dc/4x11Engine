#include "Device.h"
#include <stdexcept>

namespace d3d10 {
namespace {

ComPtr<IDXGISwapChain>          g_Swap;
ComPtr<ID3D10Device>            g_Dev;
ComPtr<ID3D10RenderTargetView>  g_RTV;
ComPtr<ID3D10DepthStencilView>  g_DSV;
ComPtr<ID3D10Texture2D>         g_DepthTex;
int g_Width = 0;
int g_Height = 0;

static void ThrowIfFailed(HRESULT hr, const char* msg)
{
    if (FAILED(hr)) throw std::runtime_error(msg);
}

static void BuildRenderTargets()
{
    ComPtr<ID3D10Texture2D> back;
    g_Swap->GetBuffer(0, __uuidof(ID3D10Texture2D), (void**)back.addr());
    ThrowIfFailed(g_Dev->CreateRenderTargetView(back.get(), nullptr, g_RTV.addr()),
                  "CreateRenderTargetView failed");

    D3D10_TEXTURE2D_DESC dd = {};
    dd.Width            = (UINT)g_Width;
    dd.Height           = (UINT)g_Height;
    dd.MipLevels        = 1;
    dd.ArraySize        = 1;
    dd.Format           = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage            = D3D10_USAGE_DEFAULT;
    dd.BindFlags        = D3D10_BIND_DEPTH_STENCIL;
    ThrowIfFailed(g_Dev->CreateTexture2D(&dd, nullptr, g_DepthTex.addr()),
                  "CreateTexture2D (depth) failed");
    ThrowIfFailed(g_Dev->CreateDepthStencilView(g_DepthTex.get(), nullptr, g_DSV.addr()),
                  "CreateDepthStencilView failed");

    ID3D10RenderTargetView* rtvRaw = g_RTV.get();
    g_Dev->OMSetRenderTargets(1, &rtvRaw, g_DSV.get());

    D3D10_VIEWPORT vp = { 0, 0, (UINT)g_Width, (UINT)g_Height, 0.f, 1.f };
    g_Dev->RSSetViewports(1, &vp);
}

} // anonymous namespace

namespace Device {

bool Init(HWND hwnd, int width, int height)
{
    g_Width = width;
    g_Height = height;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount                        = 1;
    sd.BufferDesc.Width                   = (UINT)g_Width;
    sd.BufferDesc.Height                  = (UINT)g_Height;
    sd.BufferDesc.Format                  = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator   = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage                        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow                       = hwnd;
    sd.SampleDesc.Count                   = 1;
    sd.Windowed                           = TRUE;

    HRESULT hr = D3D10CreateDeviceAndSwapChain(
        nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0,
        D3D10_SDK_VERSION, &sd, g_Swap.addr(), g_Dev.addr());
    if (FAILED(hr)) {
        hr = D3D10CreateDeviceAndSwapChain(
            nullptr, D3D10_DRIVER_TYPE_WARP, nullptr, 0,
            D3D10_SDK_VERSION, &sd, g_Swap.addr(), g_Dev.addr());
        if (FAILED(hr)) {
            MessageBoxA(nullptr, "D3D10 init failed", "Error", MB_OK);
            return false;
        }
    }

    BuildRenderTargets();
    return true;
}

void Shutdown()
{
    g_DSV.release();
    g_DepthTex.release();
    g_RTV.release();
    g_Swap.release();
    g_Dev.release();
}

void Clear(float r, float g, float b, float a)
{
    float col[4] = { r, g, b, a };
    ID3D10RenderTargetView* rtvRaw = g_RTV.get();
    g_Dev->OMSetRenderTargets(1, &rtvRaw, g_DSV.get());
    g_Dev->ClearRenderTargetView(g_RTV.get(), col);
    g_Dev->ClearDepthStencilView(g_DSV.get(), D3D10_CLEAR_DEPTH, 1.f, 0);
}

void Present()
{
    g_Swap->Present(1, 0);
}

void Resize(int width, int height)
{
    if (width == g_Width && height == g_Height) return;
    g_Width = width;
    g_Height = height;

    ID3D10RenderTargetView* nullRtv = nullptr;
    g_Dev->OMSetRenderTargets(1, &nullRtv, nullptr);
    g_RTV.release();
    g_DSV.release();
    g_DepthTex.release();

    HRESULT hr = g_Swap->ResizeBuffers(0, (UINT)g_Width, (UINT)g_Height,
                                        DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr)) BuildRenderTargets();
}

ID3D10Device* GetD3D() { return g_Dev.get(); }
IDXGISwapChain* GetSwapChain() { return g_Swap.get(); }
ID3D10RenderTargetView* GetRTV() { return g_RTV.get(); }
ID3D10DepthStencilView* GetDSV() { return g_DSV.get(); }

} // namespace Device
} // namespace d3d10
