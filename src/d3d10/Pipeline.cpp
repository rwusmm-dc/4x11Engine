#include "Pipeline.h"
#include "Device.h"
#include "core/Window.h"
#include <d3dcompiler.h>
#include <DirectXMath.h>
#include <stdexcept>
#include <string>
#include <cstring>
#include <vector>

using namespace DirectX;

namespace d3d10 {
namespace {

ComPtr<ID3D10RasterizerState>   g_RS;
ComPtr<ID3D10DepthStencilState> g_DSState;
ComPtr<ID3D10VertexShader>      g_VS;
ComPtr<ID3D10VertexShader>      g_VSInstanced;
ComPtr<ID3D10PixelShader>       g_PS;
ComPtr<ID3D10InputLayout>       g_Layout;
ComPtr<ID3D10InputLayout>       g_LayoutInstanced;
ComPtr<ID3D10Buffer>            g_CB;
ComPtr<ID3D10Buffer>            g_LightCB;

// Instancing state
ComPtr<ID3D10Buffer>                g_InstanceBuf;
ComPtr<ID3D10ShaderResourceView>    g_InstanceSRV;
int                                 g_InstanceCapacity = 0;

XMMATRIX g_View;
XMMATRIX g_Proj;
XMMATRIX g_VP;

LightData g_LightData = {};

int g_DrawCalls = 0;

struct CachedMesh {
    uint64_t entityId = 0;
    ComPtr<ID3D10Buffer> vb;
    ComPtr<ID3D10Buffer> ib;
    int indexCount = 0;
    DXGI_FORMAT idxFmt = DXGI_FORMAT_R16_UINT;
    int vertCount = 0;
};

std::vector<CachedMesh> g_MeshCache;

static void ThrowIfFailed(HRESULT hr, const char* msg)
{
    if (FAILED(hr)) throw std::runtime_error(msg);
}

static const char* g_HLSL = R"HLSL(
StructuredBuffer<float4x4> InstanceWorlds : register(t0);
cbuffer PerFrame : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
};
cbuffer LightCB : register(b1)
{
    float4 gSunDir;      // xyz = direction TOWARDS sun (normalized)
    float4 gSunColor;    // rgb = color, a = intensity
    float4 gPointPos;    // xyz = world position
    float4 gPointColor;  // rgb = color, a = intensity
    float4 gPointParam;  // x = range
    float4 gAmbient;     // rgb = ambient
    float4 gCameraPos;   // xyz = camera pos
    float4 gFlags;       // x = sunEnabled, y = pointEnabled
};
struct VSIn  { float3 pos : POSITION; float3 nrm : NORMAL; float3 col : COLOR; };
struct PSIn  { float4 pos : SV_POSITION; float3 col : COLOR; float3 normal : TEXCOORD0; float3 worldPos : TEXCOORD1; };
PSIn VSMain(VSIn v)
{
    PSIn o;
    float4 wp = mul(float4(v.pos, 1.0f), gWorld);
    o.pos = mul(wp, gViewProj);
    o.normal = mul(v.nrm, (float3x3)gWorld);
    o.worldPos = wp.xyz;
    o.col = v.col;
    return o;
}
PSIn VSMainInstanced(VSIn v, uint instanceId : SV_InstanceID)
{
    PSIn o;
    float4x4 instWorld = InstanceWorlds[instanceId];
    float4 wp = mul(float4(v.pos, 1.0f), instWorld);
    o.pos = mul(wp, gViewProj);
    o.normal = mul(v.nrm, (float3x3)instWorld);
    o.worldPos = wp.xyz;
    o.col = v.col;
    return o;
}
float4 PSMain(PSIn p) : SV_TARGET
{
    float3 n = normalize(p.normal);
    float3 lighting = gAmbient.rgb;

    // Directional sun light
    if (gFlags.x > 0.5)
    {
        float3 ld = normalize(gSunDir.xyz);
        float diff = max(dot(n, ld), 0.0);
        lighting += gSunColor.rgb * gSunColor.a * diff;
    }

    float3 lit = p.col * lighting;

    // Point light (additive - not modulated by vertex color)
    if (gFlags.y > 0.5)
    {
        float3 toLight = gPointPos.xyz - p.worldPos;
        float dist2 = dot(toLight, toLight);
        float range2 = max(gPointParam.x * gPointParam.x, 0.0001);
        float atten = max(0.0, 1.0 - dist2 / range2);
        float3 ld = normalize(toLight);
        float diff = max(dot(n, ld), 0.0) * atten;
        lit += gPointColor.rgb * gPointColor.a * diff;
    }

    return float4(lit, 1.0f);
}
)HLSL";

static CachedMesh* FindCachedMesh(uint64_t entityId)
{
    for (auto& cm : g_MeshCache)
        if (cm.entityId == entityId)
            return &cm;
    return nullptr;
}

} // anonymous namespace

namespace Pipeline {

bool Init(int, int)
{
    ID3D10Device* dev = Device::GetD3D();
    if (!dev) return false;

    D3D10_RASTERIZER_DESC rd = {};
    rd.FillMode                 = D3D10_FILL_SOLID;
    rd.CullMode                 = D3D10_CULL_BACK;
    rd.FrontCounterClockwise    = FALSE;
    rd.DepthClipEnable          = TRUE;
    rd.ScissorEnable            = FALSE;
    rd.MultisampleEnable        = FALSE;
    rd.AntialiasedLineEnable    = FALSE;
    rd.DepthBias                = 0;
    rd.DepthBiasClamp           = 0.f;
    rd.SlopeScaledDepthBias     = 0.f;
    ThrowIfFailed(dev->CreateRasterizerState(&rd, g_RS.addr()),
                  "CreateRasterizerState failed");

    D3D10_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable              = TRUE;
    dsd.DepthWriteMask           = D3D10_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc                = D3D10_COMPARISON_LESS;
    ThrowIfFailed(dev->CreateDepthStencilState(&dsd, g_DSState.addr()),
                  "CreateDepthStencilState failed");

    auto CompileShader = [&](const char* src, const char* target, const char* entry,
                             ComPtr<ID3D10Blob>& blob) -> bool {
        ComPtr<ID3D10Blob> err;
        HRESULT hr = D3DCompile(src, strlen(src), nullptr, nullptr, nullptr,
                                entry, target, 0, 0, blob.addr(), err.addr());
        if (FAILED(hr)) {
            std::string m = std::string(target) + " " + entry + " failed";
            if (err) m += ": " + std::string((char*)err->GetBufferPointer());
            MessageBoxA(nullptr, m.c_str(), "Error", MB_OK);
            return false;
        }
        return true;
    };

    ComPtr<ID3D10Blob> vsBlob, vsInstBlob, psBlob;

    if (!CompileShader(g_HLSL, "vs_4_0", "VSMain", vsBlob) ||
        !CompileShader(g_HLSL, "vs_4_0", "VSMainInstanced", vsInstBlob) ||
        !CompileShader(g_HLSL, "ps_4_0", "PSMain", psBlob))
        return false;

    ThrowIfFailed(dev->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), g_VS.addr()),
        "CreateVertexShader failed");
    ThrowIfFailed(dev->CreateVertexShader(
        vsInstBlob->GetBufferPointer(), vsInstBlob->GetBufferSize(), g_VSInstanced.addr()),
        "CreateVertexShader instanced failed");
    ThrowIfFailed(dev->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), g_PS.addr()),
        "CreatePixelShader failed");

    D3D10_INPUT_ELEMENT_DESC elems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D10_INPUT_PER_VERTEX_DATA, 0 },
    };
    ThrowIfFailed(dev->CreateInputLayout(
        elems, 3,
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
        g_Layout.addr()),
        "CreateInputLayout failed");
    ThrowIfFailed(dev->CreateInputLayout(
        elems, 3,
        vsInstBlob->GetBufferPointer(), vsInstBlob->GetBufferSize(),
        g_LayoutInstanced.addr()),
        "CreateInputLayout instanced failed");

    D3D10_BUFFER_DESC bd = {};
    bd.ByteWidth            = sizeof(XMFLOAT4X4) * 2;
    bd.Usage                = D3D10_USAGE_DYNAMIC;
    bd.BindFlags            = D3D10_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags       = D3D10_CPU_ACCESS_WRITE;
    ThrowIfFailed(dev->CreateBuffer(&bd, nullptr, g_CB.addr()),
                  "CB creation failed");

    D3D10_BUFFER_DESC lbd = {};
    lbd.ByteWidth            = sizeof(LightData);
    lbd.Usage                = D3D10_USAGE_DYNAMIC;
    lbd.BindFlags            = D3D10_BIND_CONSTANT_BUFFER;
    lbd.CPUAccessFlags       = D3D10_CPU_ACCESS_WRITE;
    ThrowIfFailed(dev->CreateBuffer(&lbd, nullptr, g_LightCB.addr()),
                  "LightCB creation failed");

    g_View = XMMatrixIdentity();
    g_Proj = XMMatrixIdentity();
    g_VP = XMMatrixIdentity();

    // Sensible defaults so lighting isn't dark/black before the engine
    // pushes its first SetLightData call.
    g_LightData.sunDir[0]  = 0.35f;
    g_LightData.sunDir[1]  = 0.85f;
    g_LightData.sunDir[2]  = 0.35f;
    g_LightData.sunColor[0] = 1.0f;
    g_LightData.sunColor[1] = 0.95f;
    g_LightData.sunColor[2] = 0.85f;
    g_LightData.sunColor[3] = 1.0f;
    g_LightData.ambient[0] = 0.08f;
    g_LightData.ambient[1] = 0.08f;
    g_LightData.ambient[2] = 0.10f;
    g_LightData.flags[0] = 1.0f; // sun enabled
    g_LightData.flags[1] = 0.0f; // point disabled

    return true;
}

void Shutdown()
{
    g_MeshCache.clear();
    g_InstanceSRV.release();
    g_InstanceBuf.release();
    g_LightCB.release();
    g_CB.release();
    g_LayoutInstanced.release();
    g_Layout.release();
    g_PS.release();
    g_VSInstanced.release();
    g_VS.release();
    g_DSState.release();
    g_RS.release();
}

void Bind()
{
    g_DrawCalls = 0;
    ID3D10Device* dev = Device::GetD3D();

    dev->RSSetState(g_RS.get());
    dev->OMSetDepthStencilState(g_DSState.get(), 0);

    ID3D10RenderTargetView* rtvRaw = Device::GetRTV();
    ID3D10DepthStencilView* dsvRaw = Device::GetDSV();
    dev->OMSetRenderTargets(1, &rtvRaw, dsvRaw);

    D3D10_VIEWPORT vp = { 0, 0, (UINT)Window::Width(), (UINT)Window::Height(), 0.f, 1.f };
    dev->RSSetViewports(1, &vp);

    dev->VSSetShader(g_VS.get());
    dev->PSSetShader(g_PS.get());
    dev->IASetInputLayout(g_Layout.get());
    dev->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D10Buffer* cbs[] = { g_CB.get(), g_LightCB.get() };
    dev->VSSetConstantBuffers(0, 2, cbs);
    dev->PSSetConstantBuffers(0, 2, cbs);
}

void SetViewProj(XMMATRIX view, XMMATRIX proj)
{
    g_View = view;
    g_Proj = proj;
    g_VP = view * proj;
}

void DrawEntity(uint64_t entityId, XMMATRIX world,
                const float* verts, int vertCount,
                const uint32_t* indices, int indexCount,
                bool meshDirty)
{
    ID3D10Device* dev = Device::GetD3D();
    if (!dev) return;

    CachedMesh* cache = FindCachedMesh(entityId);
    bool needsNew = meshDirty || !cache || (cache && !cache->vb.get());

    if (needsNew) {
        if (cache) {
            cache->vb.release();
            cache->ib.release();
        } else {
            g_MeshCache.emplace_back();
            cache = &g_MeshCache.back();
            cache->entityId = entityId;
        }

        D3D10_BUFFER_DESC bd = {};
        bd.ByteWidth  = vertCount * 9 * (UINT)sizeof(float);
        bd.Usage      = D3D10_USAGE_DEFAULT;
        bd.BindFlags  = D3D10_BIND_VERTEX_BUFFER;
        D3D10_SUBRESOURCE_DATA init = {};
        init.pSysMem  = verts;
        if (FAILED(dev->CreateBuffer(&bd, &init, cache->vb.addr()))) {
            RemoveEntityMesh(entityId);
            return;
        }

        DXGI_FORMAT fmt = DXGI_FORMAT_R16_UINT;
        if (indexCount > 65535) {
            bd = {};
            bd.ByteWidth  = indexCount * (UINT)sizeof(uint32_t);
            bd.Usage      = D3D10_USAGE_DEFAULT;
            bd.BindFlags  = D3D10_BIND_INDEX_BUFFER;
            init = {};
            init.pSysMem  = indices;
            if (FAILED(dev->CreateBuffer(&bd, &init, cache->ib.addr()))) {
                cache->vb.release();
                RemoveEntityMesh(entityId);
                return;
            }
            fmt = DXGI_FORMAT_R32_UINT;
        } else {
            std::vector<uint16_t> idx16;
            idx16.reserve(indexCount);
            for (int i = 0; i < indexCount; i++)
                idx16.push_back((uint16_t)indices[i]);
            bd = {};
            bd.ByteWidth  = indexCount * (UINT)sizeof(uint16_t);
            bd.Usage      = D3D10_USAGE_DEFAULT;
            bd.BindFlags  = D3D10_BIND_INDEX_BUFFER;
            init = {};
            init.pSysMem  = idx16.data();
            if (FAILED(dev->CreateBuffer(&bd, &init, cache->ib.addr()))) {
                cache->vb.release();
                RemoveEntityMesh(entityId);
                return;
            }
        }

        cache->indexCount = indexCount;
        cache->idxFmt     = fmt;
        cache->vertCount  = vertCount;
    }

    if (!cache->vb.get() || !cache->ib.get()) return;

    struct { XMFLOAT4X4 world; XMFLOAT4X4 viewProj; } cbData;
    XMStoreFloat4x4(&cbData.world, XMMatrixTranspose(world));
    XMStoreFloat4x4(&cbData.viewProj, XMMatrixTranspose(g_VP));
    void* mapped = nullptr;
    if (SUCCEEDED(g_CB->Map(D3D10_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped, &cbData, sizeof(cbData));
        g_CB->Unmap();
    }

    if (SUCCEEDED(g_LightCB->Map(D3D10_MAP_WRITE_DISCARD, 0, &mapped))) {
        memcpy(mapped, &g_LightData, sizeof(g_LightData));
        g_LightCB->Unmap();
    }

    ID3D10Buffer* vbRaw = cache->vb.get();
    UINT stride = 9 * sizeof(float), offset = 0;
    dev->IASetVertexBuffers(0, 1, &vbRaw, &stride, &offset);
    dev->IASetIndexBuffer(cache->ib.get(), cache->idxFmt, 0);
    dev->DrawIndexed(cache->indexCount, 0, 0);
    g_DrawCalls++;
}

void DrawEntityInstanced(uint64_t entityId,
                const XMMATRIX* worlds, int instanceCount,
                const float* verts, int vertCount,
                const uint32_t* indices, int indexCount,
                bool meshDirty)
{
    // D3D10 lacks structured buffer support; draw non-instanced as fallback.
    for (int i = 0; i < instanceCount; i++)
        DrawEntity(entityId, worlds[i], verts, vertCount, indices, indexCount, i == 0 ? meshDirty : false);
}

void RemoveEntityMesh(uint64_t entityId)
{
    for (size_t i = 0; i < g_MeshCache.size(); i++) {
        if (g_MeshCache[i].entityId == entityId) {
            g_MeshCache.erase(g_MeshCache.begin() + i);
            return;
        }
    }
}

void SetLightData(const LightData& data)
{
    g_LightData = data;
}

int DrawCallCount()
{
    return g_DrawCalls;
}

} // namespace Pipeline
}
