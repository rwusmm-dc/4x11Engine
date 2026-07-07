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

namespace d3d11 {
namespace {

ComPtr<ID3D11RasterizerState>   g_RS;
ComPtr<ID3D11DepthStencilState> g_DSState;
ComPtr<ID3D11VertexShader>      g_VS;
ComPtr<ID3D11VertexShader>      g_VSInstanced;
ComPtr<ID3D11PixelShader>       g_PS;
ComPtr<ID3D11InputLayout>       g_Layout;
ComPtr<ID3D11InputLayout>       g_LayoutInstanced;
ComPtr<ID3D11Buffer>            g_CB;
ComPtr<ID3D11Buffer>            g_LightCB;

// Instancing state: a structured buffer + SRV for per-instance world matrices
ComPtr<ID3D11Buffer>                g_InstanceBuf;
ComPtr<ID3D11ShaderResourceView>    g_InstanceSRV;
int                                 g_InstanceCapacity = 0;

XMMATRIX g_View;
XMMATRIX g_Proj;

LightData g_LightData = {};

int g_DrawCalls = 0;

struct CachedMesh {
    uint64_t entityId = 0;
    ComPtr<ID3D11Buffer> vb;
    ComPtr<ID3D11Buffer> ib;
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
cbuffer CBMatrix : register(b0)
{
    matrix World;
    matrix View;
    matrix Projection;
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
    float4 wp  = mul(float4(v.pos, 1.0f), World);
    float4 vp  = mul(wp, View);
    o.pos      = mul(vp, Projection);
    o.normal   = mul(v.nrm, (float3x3)World);
    o.worldPos = wp.xyz;
    o.col      = v.col;
    return o;
}
PSIn VSMainInstanced(VSIn v, uint instanceId : SV_InstanceID)
{
    PSIn o;
    matrix instWorld = InstanceWorlds[instanceId];
    float4 wp  = mul(float4(v.pos, 1.0f), instWorld);
    float4 vp  = mul(wp, View);
    o.pos      = mul(vp, Projection);
    o.normal   = mul(v.nrm, (float3x3)instWorld);
    o.worldPos = wp.xyz;
    o.col      = v.col;
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

struct CBData {
    XMMATRIX World;
    XMMATRIX View;
    XMMATRIX Projection;
};

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
    ID3D11Device* dev = Device::GetD3D();
    if (!dev) return false;

    D3D11_RASTERIZER_DESC rd = {};
    rd.FillMode              = D3D11_FILL_SOLID;
    rd.CullMode              = D3D11_CULL_BACK;
    rd.FrontCounterClockwise = FALSE;
    rd.DepthClipEnable       = TRUE;
    rd.ScissorEnable         = FALSE;
    rd.MultisampleEnable     = FALSE;
    rd.AntialiasedLineEnable = FALSE;
    rd.DepthBias             = 0;
    rd.DepthBiasClamp        = 0.f;
    rd.SlopeScaledDepthBias  = 0.f;
    ThrowIfFailed(dev->CreateRasterizerState(&rd, g_RS.addr()),
                  "CreateRasterizerState failed");

    D3D11_DEPTH_STENCIL_DESC dsd = {};
    dsd.DepthEnable      = TRUE;
    dsd.DepthWriteMask   = D3D11_DEPTH_WRITE_MASK_ALL;
    dsd.DepthFunc        = D3D11_COMPARISON_LESS;
    ThrowIfFailed(dev->CreateDepthStencilState(&dsd, g_DSState.addr()),
                  "CreateDepthStencilState failed");

    auto CompileShader = [&](const char* src, const char* target, const char* entry,
                             ComPtr<ID3DBlob>& blob) -> bool {
        ComPtr<ID3DBlob> err;
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

    ComPtr<ID3DBlob> vsBlob, vsInstBlob, psBlob;

    if (!CompileShader(g_HLSL, "vs_4_0", "VSMain", vsBlob) ||
        !CompileShader(g_HLSL, "vs_4_0", "VSMainInstanced", vsInstBlob) ||
        !CompileShader(g_HLSL, "ps_4_0", "PSMain", psBlob))
        return false;

    ThrowIfFailed(dev->CreateVertexShader(
        vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, g_VS.addr()),
        "CreateVertexShader failed");
    ThrowIfFailed(dev->CreateVertexShader(
        vsInstBlob->GetBufferPointer(), vsInstBlob->GetBufferSize(), nullptr, g_VSInstanced.addr()),
        "CreateVertexShader instanced failed");
    ThrowIfFailed(dev->CreatePixelShader(
        psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, g_PS.addr()),
        "CreatePixelShader failed");

    D3D11_INPUT_ELEMENT_DESC elems[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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

    D3D11_BUFFER_DESC bd = {};
    bd.ByteWidth     = sizeof(CBData);
    bd.Usage         = D3D11_USAGE_DEFAULT;
    bd.BindFlags     = D3D11_BIND_CONSTANT_BUFFER;
    ThrowIfFailed(dev->CreateBuffer(&bd, nullptr, g_CB.addr()),
                  "CB creation failed");

    D3D11_BUFFER_DESC lbd = {};
    lbd.ByteWidth     = sizeof(LightData);
    lbd.Usage         = D3D11_USAGE_DEFAULT;
    lbd.BindFlags     = D3D11_BIND_CONSTANT_BUFFER;
    ThrowIfFailed(dev->CreateBuffer(&lbd, nullptr, g_LightCB.addr()),
                  "LightCB creation failed");

    g_View = XMMatrixIdentity();
    g_Proj = XMMatrixIdentity();

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
    ID3D11DeviceContext* ctx = Device::GetCtx();

    ctx->RSSetState(g_RS.get());
    ctx->OMSetDepthStencilState(g_DSState.get(), 0);

    ID3D11RenderTargetView* rtvRaw = Device::GetRTV();
    ID3D11DepthStencilView* dsvRaw = Device::GetDSV();
    ctx->OMSetRenderTargets(1, &rtvRaw, dsvRaw);

    D3D11_VIEWPORT vp = { 0, 0, (FLOAT)Window::Width(), (FLOAT)Window::Height(), 0.f, 1.f };
    ctx->RSSetViewports(1, &vp);

    ctx->VSSetShader(g_VS.get(), nullptr, 0);
    ctx->PSSetShader(g_PS.get(), nullptr, 0);
    ctx->IASetInputLayout(g_Layout.get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11Buffer* cbs[] = { g_CB.get(), g_LightCB.get() };
    ctx->VSSetConstantBuffers(0, 2, cbs);
    ctx->PSSetConstantBuffers(0, 2, cbs);
}

void SetViewProj(XMMATRIX view, XMMATRIX proj)
{
    g_View = view;
    g_Proj = proj;
}

void DrawEntity(uint64_t entityId, XMMATRIX world,
                const float* verts, int vertCount,
                const uint32_t* indices, int indexCount,
                bool meshDirty)
{
    ID3D11Device* dev = Device::GetD3D();
    ID3D11DeviceContext* ctx = Device::GetCtx();
    if (!dev || !ctx) return;

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

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth  = vertCount * 9 * (UINT)sizeof(float);
        bd.Usage      = D3D11_USAGE_DEFAULT;
        bd.BindFlags  = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem  = verts;
        if (FAILED(dev->CreateBuffer(&bd, &init, cache->vb.addr()))) {
            RemoveEntityMesh(entityId);
            return;
        }

        DXGI_FORMAT fmt = DXGI_FORMAT_R16_UINT;
        if (indexCount > 65535) {
            bd = {};
            bd.ByteWidth  = indexCount * (UINT)sizeof(uint32_t);
            bd.Usage      = D3D11_USAGE_DEFAULT;
            bd.BindFlags  = D3D11_BIND_INDEX_BUFFER;
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
            bd.Usage      = D3D11_USAGE_DEFAULT;
            bd.BindFlags  = D3D11_BIND_INDEX_BUFFER;
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

    CBData cbData;
    cbData.World      = XMMatrixTranspose(world);
    cbData.View       = XMMatrixTranspose(g_View);
    cbData.Projection = XMMatrixTranspose(g_Proj);
    ctx->UpdateSubresource(g_CB.get(), 0, nullptr, &cbData, 0, 0);
    ctx->UpdateSubresource(g_LightCB.get(), 0, nullptr, &g_LightData, 0, 0);

    ID3D11Buffer* vbRaw = cache->vb.get();
    UINT stride = 9 * sizeof(float), offset = 0;
    ctx->IASetVertexBuffers(0, 1, &vbRaw, &stride, &offset);
    ctx->IASetIndexBuffer(cache->ib.get(), cache->idxFmt, 0);
    ctx->DrawIndexed(cache->indexCount, 0, 0);
    g_DrawCalls++;
}

void DrawEntityInstanced(uint64_t entityId,
                const XMMATRIX* worlds, int instanceCount,
                const float* verts, int vertCount,
                const uint32_t* indices, int indexCount,
                bool meshDirty)
{
    ID3D11Device* dev = Device::GetD3D();
    ID3D11DeviceContext* ctx = Device::GetCtx();
    if (!dev || !ctx || instanceCount < 1) return;

    // Ensure instance buffer is large enough
    int needed = instanceCount * (int)sizeof(XMMATRIX);
    if (needed > g_InstanceCapacity) {
        g_InstanceSRV.release();
        g_InstanceBuf.release();

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth              = needed;
        bd.Usage                  = D3D11_USAGE_DEFAULT;
        bd.BindFlags              = D3D11_BIND_SHADER_RESOURCE;
        bd.MiscFlags              = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        bd.StructureByteStride    = sizeof(XMMATRIX);
        if (FAILED(dev->CreateBuffer(&bd, nullptr, g_InstanceBuf.addr())))
            return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format              = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension       = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements  = instanceCount;
        if (FAILED(dev->CreateShaderResourceView(g_InstanceBuf.get(), &srvDesc, g_InstanceSRV.addr()))) {
            g_InstanceBuf.release();
            return;
        }
        g_InstanceCapacity = needed;
    }

    // Upload instance data (transpose to column-major for HLSL)
    std::vector<XMMATRIX> transposed(instanceCount);
    for (int i = 0; i < instanceCount; i++)
        transposed[i] = XMMatrixTranspose(worlds[i]);
    ctx->UpdateSubresource(g_InstanceBuf.get(), 0, nullptr, transposed.data(), 0, 0);

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

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth  = vertCount * 9 * (UINT)sizeof(float);
        bd.Usage      = D3D11_USAGE_DEFAULT;
        bd.BindFlags  = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem  = verts;
        if (FAILED(dev->CreateBuffer(&bd, &init, cache->vb.addr()))) {
            RemoveEntityMesh(entityId);
            return;
        }

        DXGI_FORMAT fmt = DXGI_FORMAT_R16_UINT;
        if (indexCount > 65535) {
            bd = {};
            bd.ByteWidth  = indexCount * (UINT)sizeof(uint32_t);
            bd.Usage      = D3D11_USAGE_DEFAULT;
            bd.BindFlags  = D3D11_BIND_INDEX_BUFFER;
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
            bd.Usage      = D3D11_USAGE_DEFAULT;
            bd.BindFlags  = D3D11_BIND_INDEX_BUFFER;
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

    CBData cbData;
    cbData.World      = XMMatrixIdentity(); // unused by instanced VS
    cbData.View       = XMMatrixTranspose(g_View);
    cbData.Projection = XMMatrixTranspose(g_Proj);
    ctx->UpdateSubresource(g_CB.get(), 0, nullptr, &cbData, 0, 0);
    ctx->UpdateSubresource(g_LightCB.get(), 0, nullptr, &g_LightData, 0, 0);

    // Bind instanced shader + instance SRV
    ctx->VSSetShader(g_VSInstanced.get(), nullptr, 0);
    ctx->IASetInputLayout(g_LayoutInstanced.get());
    ID3D11ShaderResourceView* srvRaw = g_InstanceSRV.get();
    ctx->VSSetShaderResources(0, 1, &srvRaw);

    ID3D11Buffer* vbRaw = cache->vb.get();
    UINT stride = 9 * sizeof(float), offset = 0;
    ctx->IASetVertexBuffers(0, 1, &vbRaw, &stride, &offset);
    ctx->IASetIndexBuffer(cache->ib.get(), cache->idxFmt, 0);
    ctx->DrawIndexedInstanced(cache->indexCount, instanceCount, 0, 0, 0);
    g_DrawCalls++;

    // Restore non-instanced shader
    ctx->VSSetShader(g_VS.get(), nullptr, 0);
    ctx->IASetInputLayout(g_Layout.get());
    ID3D11ShaderResourceView* nullSRV = nullptr;
    ctx->VSSetShaderResources(0, 1, &nullSRV);
}

void SetLightData(const LightData& data)
{
    g_LightData = data;
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

int DrawCallCount()
{
    return g_DrawCalls;
}

} // namespace Pipeline
} // namespace d3d11