#include "Device.h"
#include <stdexcept>

namespace d3d11 {
namespace {

ComPtr<IDXGISwapChain>          g_Swap;
ComPtr<ID3D11Device>            g_Dev;
ComPtr<ID3D11DeviceContext>     g_Ctx;
ComPtr<ID3D11RenderTargetView>  g_RTV;
ComPtr<ID3D11DepthStencilView>  g_DSV;
ComPtr<ID3D11Texture2D>         g_DepthTex;
int g_Width  = 0;
int g_Height = 0;

static void ThrowIfFailed(HRESULT hr, const char* msg)
{
    if (FAILED(hr)) throw std::runtime_error(msg);
}

static void BuildRenderTargets()
{
    ComPtr<ID3D11Texture2D> back;
    g_Swap->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)back.addr());
    ThrowIfFailed(g_Dev->CreateRenderTargetView(back.get(), nullptr, g_RTV.addr()),
                  "CreateRenderTargetView failed");

    D3D11_TEXTURE2D_DESC dd = {};
    dd.Width            = (UINT)g_Width;
    dd.Height           = (UINT)g_Height;
    dd.MipLevels        = 1;
    dd.ArraySize        = 1;
    dd.Format           = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dd.SampleDesc.Count = 1;
    dd.Usage            = D3D11_USAGE_DEFAULT;
    dd.BindFlags        = D3D11_BIND_DEPTH_STENCIL;
    ThrowIfFailed(g_Dev->CreateTexture2D(&dd, nullptr, g_DepthTex.addr()),
                  "CreateTexture2D (depth) failed");

    D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format        = dd.Format;
    dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;
    ThrowIfFailed(g_Dev->CreateDepthStencilView(g_DepthTex.get(), &dsvDesc, g_DSV.addr()),
                  "CreateDepthStencilView failed");

    ID3D11RenderTargetView* rtvRaw = g_RTV.get();
    g_Ctx->OMSetRenderTargets(1, &rtvRaw, g_DSV.get());

    D3D11_VIEWPORT vp = { 0, 0, (FLOAT)g_Width, (FLOAT)g_Height, 0.f, 1.f };
    g_Ctx->RSSetViewports(1, &vp);
}

} // anonymous namespace

namespace Device {

bool Init(HWND hwnd, int width, int height)
{
    g_Width  = width;
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

    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL selected;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
        &sd, g_Swap.addr(), g_Dev.addr(), &selected, g_Ctx.addr());

    if (FAILED(hr)) {
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
            &sd, g_Swap.addr(), g_Dev.addr(), &selected, g_Ctx.addr());
        if (FAILED(hr)) {
            MessageBoxA(nullptr, "D3D11 init failed", "Error", MB_OK);
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
    g_Ctx.release();
    g_Swap.release();
    g_Dev.release();
}

void Clear(float r, float g, float b, float a)
{
    float col[4] = { r, g, b, a };
    ID3D11RenderTargetView* rtvRaw = g_RTV.get();
    g_Ctx->OMSetRenderTargets(1, &rtvRaw, g_DSV.get());
    g_Ctx->ClearRenderTargetView(g_RTV.get(), col);
    g_Ctx->ClearDepthStencilView(g_DSV.get(), D3D11_CLEAR_DEPTH, 1.f, 0);
}

void Present()
{
    g_Swap->Present(1, 0);
}

void Resize(int width, int height)
{
    if (width == g_Width && height == g_Height) return;
    g_Width  = width;
    g_Height = height;

    ID3D11RenderTargetView* nullRtv = nullptr;
    g_Ctx->OMSetRenderTargets(1, &nullRtv, nullptr);
    g_RTV.release();
    g_DSV.release();
    g_DepthTex.release();

    HRESULT hr = g_Swap->ResizeBuffers(0, (UINT)g_Width, (UINT)g_Height,
                                        DXGI_FORMAT_UNKNOWN, 0);
    if (SUCCEEDED(hr)) BuildRenderTargets();
}

ID3D11Device*        GetD3D()      { return g_Dev.get(); }
ID3D11DeviceContext* GetCtx()      { return g_Ctx.get(); }
IDXGISwapChain*      GetSwapChain(){ return g_Swap.get(); }
ID3D11RenderTargetView* GetRTV()   { return g_RTV.get(); }
ID3D11DepthStencilView* GetDSV()   { return g_DSV.get(); }

} // namespace Device
} // namespace d3d11