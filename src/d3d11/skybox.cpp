#include "skybox.h"
#include "Device.h"
#include "core/ComPtr.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <cmath>
#include <cstring>
#include <stdexcept>

using namespace DirectX;

namespace d3d11 {
namespace {

ComPtr<ID3D11VertexShader>      g_VS;
ComPtr<ID3D11PixelShader>       g_PS;
ComPtr<ID3D11Buffer>            g_CB;
ComPtr<ID3D11DepthStencilState> g_DSState;
ComPtr<ID3D11RasterizerState>   g_RSState;

float g_TimeOfDay = 12.0f;
float g_Speed     = 0.5f;
float g_SunDir[3]   = { 0.0f, 1.0f, 0.0f };
float g_SunColor[3] = { 1.0f, 0.95f, 0.8f };
float g_SkyColor[3] = { 0.4f, 0.6f, 0.9f };
bool  g_Enabled     = false;
bool  g_SkyColorOverride = false;

struct SkyCBData {
    XMMATRIX InvViewProj;
    XMFLOAT3 SunDirection;
    float Padding1;
    XMFLOAT3 SunColor;
    float Padding2;
    XMFLOAT3 SkyColor;
    float Padding3;
};

static void ThrowIfFailed(HRESULT hr, const char* msg)
{
    if (FAILED(hr)) throw std::runtime_error(msg);
}

static void UpdateSun()
{
    float angle = (g_TimeOfDay / 24.0f) * XM_2PI - XM_PIDIV2;
    float height = sinf(angle);
    float sunAlt = height * 0.8f + 0.2f;
    float sunAz  = angle;

    XMVECTOR dir = XMVectorSet(
        cosf(sunAz) * cosf(sunAlt),
        sinf(sunAlt),
        sinf(sunAz) * cosf(sunAlt),
        0.0f);
    XMStoreFloat3(reinterpret_cast<XMFLOAT3*>(g_SunDir), dir);

    float altitude = (sunAlt + 1.0f) * 0.5f;
    float sunIntensity = fmaxf(0.0f, fminf(1.0f, (altitude - 0.1f) * 1.5f));

    XMFLOAT3 sunsetColor = { 1.0f, 0.6f, 0.2f };
    XMFLOAT3 noonColor  = { 1.0f, 0.95f, 0.8f };
    float blend = fminf(1.0f, (altitude - 0.2f) * 3.0f);

    g_SunColor[0] = (sunsetColor.x * (1.0f - blend) + noonColor.x * blend) * sunIntensity;
    g_SunColor[1] = (sunsetColor.y * (1.0f - blend) + noonColor.y * blend) * sunIntensity;
    g_SunColor[2] = (sunsetColor.z * (1.0f - blend) + noonColor.z * blend) * sunIntensity;

    float skyBrightness = 0.05f + altitude * 0.95f;
    XMFLOAT3 daySky   = { 0.4f, 0.6f, 0.9f };
    XMFLOAT3 nightSky = { 0.003f, 0.003f, 0.01f };
    float dayFactor = fmaxf(0.0f, fminf(1.0f, (altitude - 0.05f) * 4.0f));

    float r = (nightSky.x * (1.0f - dayFactor) + daySky.x * dayFactor) * skyBrightness;
    float g = (nightSky.y * (1.0f - dayFactor) + daySky.y * dayFactor) * skyBrightness;
    float b = (nightSky.z * (1.0f - dayFactor) + daySky.z * dayFactor) * skyBrightness;

    if (g_SkyColorOverride) {
        r *= g_SkyColor[0];
        g *= g_SkyColor[1];
        b *= g_SkyColor[2];
    }

    g_SkyColor[0] = r;
    g_SkyColor[1] = g;
    g_SkyColor[2] = b;
}

static const char* g_HLSL = R"HLSL(
struct PSIn  {
    float4 pos : SV_POSITION;
    float3 dir : TEXCOORD0;
};
cbuffer CBSky : register(b0) {
    float4x4 InvViewProj;
    float3 SunDirection;
    float Padding1;
    float3 SunColor;
    float Padding2;
    float3 SkyColor;
    float Padding3;
};

PSIn VSMain(uint id : SV_VertexID) {
    PSIn o;
    float2 uv = float2((id << 1) & 2, id & 2);
    float4 clip = float4(uv * 2.0 - 1.0, 1.0, 1.0);
    o.pos = clip;
    float4 world = mul(clip, InvViewProj);
    o.dir = world.xyz / world.w;
    return o;
}

float4 PSMain(PSIn p) : SV_TARGET {
    float3 dir = normalize(p.dir);
    float sunDot = dot(dir, SunDirection);

    float3 sky = SkyColor;
    float horizon = 1.0 - abs(dir.y);
    float dayBright = max(SkyColor.r, max(SkyColor.g, SkyColor.b));
    sky += float3(0.15, 0.2, 0.3) * horizon * 0.25 * dayBright;

    float glow = pow(max(0.0, sunDot), 50.0) * 2.0;
    sky += SunColor * glow * 1.5;

    float disk = pow(max(0.0, sunDot), 28000.0) * 20.0;
    sky += SunColor * disk;

    return float4(sky, 1.0);
}
)HLSL";

static bool CompileShaders()
{
    ID3D11Device* dev = Device::GetD3D();
    if (!dev) return false;

    DWORD flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
    flags |= D3DCOMPILE_DEBUG;
#endif

    ComPtr<ID3DBlob> vsBlob, psBlob, err;

    HRESULT hr = D3DCompile(g_HLSL, strlen(g_HLSL), "sky", nullptr, nullptr,
                            "VSMain", "vs_4_0", flags, 0, vsBlob.addr(), err.addr());
    if (FAILED(hr)) {
        if (err) MessageBoxA(nullptr, (char*)err->GetBufferPointer(), "Sky VS Error", MB_OK);
        return false;
    }

    hr = D3DCompile(g_HLSL, strlen(g_HLSL), "sky", nullptr, nullptr,
                    "PSMain", "ps_4_0", flags, 0, psBlob.addr(), err.addr());
    if (FAILED(hr)) {
        if (err) MessageBoxA(nullptr, (char*)err->GetBufferPointer(), "Sky PS Error", MB_OK);
        return false;
    }

    ThrowIfFailed(dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                          nullptr, g_VS.addr()), "Sky VS create failed");
    ThrowIfFailed(dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                         nullptr, g_PS.addr()), "Sky PS create failed");
    return true;
}



static void CreateStates()
{
    ID3D11Device* dev = Device::GetD3D();

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable    = FALSE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsd.DepthFunc      = D3D11_COMPARISON_ALWAYS;
    ThrowIfFailed(dev->CreateDepthStencilState(&dsd, g_DSState.addr()), "Sky DS failed");

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode              = D3D11_FILL_SOLID;
    rd.CullMode              = D3D11_CULL_NONE;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable       = FALSE;
    rd.ScissorEnable         = FALSE;
    rd.MultisampleEnable     = FALSE;
    rd.AntialiasedLineEnable = FALSE;
    ThrowIfFailed(dev->CreateRasterizerState(&rd, g_RSState.addr()), "Sky RS failed");
}

static void CreateConstantBuffer()
{
    ID3D11Device* dev = Device::GetD3D();
    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth     = sizeof(SkyCBData);
    bd.Usage         = D3D11_USAGE_DEFAULT;
    bd.BindFlags     = D3D11_BIND_CONSTANT_BUFFER;
    ThrowIfFailed(dev->CreateBuffer(&bd, nullptr, g_CB.addr()), "Sky CB failed");
}

} // anonymous namespace

namespace Skybox {

bool Init()
{
    if (!CompileShaders()) return false;
    CreateStates();
    CreateConstantBuffer();
    UpdateSun();
    return true;
}

void Shutdown()
{
    g_CB.release();
    g_RSState.release();
    g_DSState.release();
    g_PS.release();
    g_VS.release();
}

void Update(float deltaTime)
{
    g_TimeOfDay += deltaTime * g_Speed;
    if (g_TimeOfDay >= 24.0f) g_TimeOfDay -= 24.0f;
    if (g_TimeOfDay < 0.0f) g_TimeOfDay += 24.0f;
    UpdateSun();
}

void Draw(const float* viewMatrix, const float* projMatrix, float aspectRatio, float totalTime)
{
    ID3D11DeviceContext* ctx = Device::GetCtx();
    if (!ctx || !g_Enabled) return;

    XMMATRIX view = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(viewMatrix));
    view.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
    XMMATRIX proj = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(projMatrix));
    XMMATRIX invViewProj = XMMatrixInverse(nullptr, view * proj);

    SkyCBData cb = {};
    cb.InvViewProj = XMMatrixTranspose(invViewProj);
    cb.SunDirection = *reinterpret_cast<const XMFLOAT3*>(g_SunDir);
    cb.SunColor     = *reinterpret_cast<const XMFLOAT3*>(g_SunColor);
    cb.SkyColor     = *reinterpret_cast<const XMFLOAT3*>(g_SkyColor);
    ctx->UpdateSubresource(g_CB.get(), 0, nullptr, &cb, 0, 0);

    ctx->OMSetDepthStencilState(g_DSState.get(), 0);
    ctx->RSSetState(g_RSState.get());

    ctx->IASetInputLayout(nullptr);
    ctx->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ctx->VSSetShader(g_VS.get(), nullptr, 0);
    ctx->PSSetShader(g_PS.get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, g_CB.addr());
    ctx->PSSetConstantBuffers(0, 1, g_CB.addr());

    ctx->Draw(3, 0);
}

TimeState GetTimeState()
{
    TimeState ts = {};
    ts.timeOfDay = g_TimeOfDay;
    ts.speed     = g_Speed;
    ts.sunDir[0] = g_SunDir[0];
    ts.sunDir[1] = g_SunDir[1];
    ts.sunDir[2] = g_SunDir[2];
    ts.sunColor[0] = g_SunColor[0];
    ts.sunColor[1] = g_SunColor[1];
    ts.sunColor[2] = g_SunColor[2];
    ts.skyColor[0] = g_SkyColor[0];
    ts.skyColor[1] = g_SkyColor[1];
    ts.skyColor[2] = g_SkyColor[2];
    return ts;
}

SunLight GetSunLight()
{
    SunLight sl = {};
    sl.direction[0] = g_SunDir[0];
    sl.direction[1] = g_SunDir[1];
    sl.direction[2] = g_SunDir[2];
    sl.color[0] = g_SunColor[0];
    sl.color[1] = g_SunColor[1];
    sl.color[2] = g_SunColor[2];
    sl.intensity = 1.0f;
    return sl;
}

void SetTimeOfDay(float hours) { g_TimeOfDay = hours; UpdateSun(); }
void SetTimeSpeed(float hoursPerSec) { g_Speed = hoursPerSec; }
void SetSkyColor(float r, float g, float b) { g_SkyColorOverride = true; g_SkyColor[0] = r; g_SkyColor[1] = g; g_SkyColor[2] = b; }
void ClearSkyColorOverride() { g_SkyColorOverride = false; }
void SetEnabled(bool enabled) { g_Enabled = enabled; }
bool IsEnabled() { return g_Enabled; }


} // namespace Skybox
} // namespace d3d11