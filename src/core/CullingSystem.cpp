#include "CullingSystem.h"
#include "ecs/ECS.h"
#include <cstring>
#include <cassert>
#include <cmath>
#include <d3dcompiler.h>
#include <limits>

using namespace DirectX;

// ---------------------------------------------------------------------------
// AABB
// ---------------------------------------------------------------------------

XMFLOAT3 AABB::GetCenter() const
{
    return XMFLOAT3(
        (min.x + max.x) * 0.5f,
        (min.y + max.y) * 0.5f,
        (min.z + max.z) * 0.5f);
}

XMFLOAT3 AABB::GetExtents() const
{
    return XMFLOAT3(
        (max.x - min.x) * 0.5f,
        (max.y - min.y) * 0.5f,
        (max.z - min.z) * 0.5f);
}

// ---------------------------------------------------------------------------
// Plane
// ---------------------------------------------------------------------------

float Plane::DistanceToPoint(const XMFLOAT3& p) const
{
    return v.x * p.x + v.y * p.y + v.z * p.z + v.w;
}

// ---------------------------------------------------------------------------
// Frustum
// ---------------------------------------------------------------------------

void Frustum::ExtractFromMatrix(const XMMATRIX& viewProj)
{
    XMFLOAT4X4 m;
    XMStoreFloat4x4(&m, viewProj);

    planes[Left].v   = XMFLOAT4(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41);
    planes[Right].v  = XMFLOAT4(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41);
    planes[Bottom].v = XMFLOAT4(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42);
    planes[Top].v    = XMFLOAT4(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42);
    planes[Near].v   = XMFLOAT4(m._13, m._23, m._33, m._43);
    planes[Far].v    = XMFLOAT4(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43);

    for (int i = 0; i < 6; ++i)
    {
        XMVECTOR p = XMLoadFloat4(&planes[i].v);
        XMVECTOR n = XMVector3Length(p);
        float len = XMVectorGetX(n);
        if (len > 1e-8f)
        {
            XMVECTOR inv = XMVectorReplicate(1.0f / len);
            p = XMVectorMultiply(p, inv);
        }
        XMStoreFloat4(&planes[i].v, p);
    }
}

bool Frustum::Intersects(const AABB& box) const
{
    XMFLOAT3 center = box.GetCenter();
    XMFLOAT3 extent = box.GetExtents();

    for (int i = 0; i < 6; ++i)
    {
        const Plane& pl = planes[i];
        float r = extent.x * fabsf(pl.v.x) +
                  extent.y * fabsf(pl.v.y) +
                  extent.z * fabsf(pl.v.z);
        float d = pl.DistanceToPoint(center);
        if (d + r < -0.01f)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// World-space AABB from local AABB + world matrix
// ---------------------------------------------------------------------------

static AABB TransformAABB(const AABB& local, const XMMATRIX& world)
{
    XMFLOAT3 c = local.GetCenter();
    XMFLOAT3 e = local.GetExtents();

    static const int corners[8][3] = {
        {-1,-1,-1},{ 1,-1,-1},{ 1, 1,-1},{-1, 1,-1},
        {-1,-1, 1},{ 1,-1, 1},{ 1, 1, 1},{-1, 1, 1}
    };

    AABB out;
    bool first = true;
    for (int i = 0; i < 8; ++i)
    {
        XMVECTOR p = XMVectorSet(
            c.x + e.x * corners[i][0],
            c.y + e.y * corners[i][1],
            c.z + e.z * corners[i][2], 1.0f);
        p = XMVector4Transform(p, world);
        float x = XMVectorGetX(p);
        float y = XMVectorGetY(p);
        float z = XMVectorGetZ(p);
        if (first)
        {
            out.min = XMFLOAT3(x, y, z);
            out.max = XMFLOAT3(x, y, z);
            first = false;
        }
        else
        {
            if (x < out.min.x) out.min.x = x;
            if (x > out.max.x) out.max.x = x;
            if (y < out.min.y) out.min.y = y;
            if (y > out.max.y) out.max.y = y;
            if (z < out.min.z) out.min.z = z;
            if (z > out.max.z) out.max.z = z;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// CullingSystem
// ---------------------------------------------------------------------------

void CullingSystem::Init(void* device, void* context, bool isD3D11)
{
    Shutdown();
    m_device = device;
    m_context = context;
    m_isD3D11 = isD3D11;

    if (isD3D11 && device)
        m_initialized = CreateProxyGeometry() && CreateProxyShaders() && CreateStates();
}

void CullingSystem::Shutdown()
{
    for (auto& kv : m_queries)
        if (kv.second) kv.second->Release();
    m_queries.clear();
    m_queryStates.clear();
    m_aabbs.clear();

    SafeRelease(m_proxyVB);
    SafeRelease(m_proxyIB);
    SafeRelease(m_proxyVS);
    SafeRelease(m_proxyPS);
    SafeRelease(m_proxyInputLayout);
    SafeRelease(m_proxyCB);
    SafeRelease(m_depthStencilStateNoWrite);
    SafeRelease(m_rasterizerStateProxy);
    m_initialized = false;
}

void CullingSystem::BeginFrame(const XMMATRIX& viewProj)
{
    m_frustum.ExtractFromMatrix(viewProj);
    m_viewProj = viewProj;
    m_visibleCount = 0;
    m_totalCount = 0;
}

void CullingSystem::ComputeAABB(const std::vector<float>& verts, AABB& outAABB)
{
    if (verts.empty())
    {
        outAABB.min = XMFLOAT3(-0.5f, -0.5f, -0.5f);
        outAABB.max = XMFLOAT3(0.5f, 0.5f, 0.5f);
        return;
    }

    float vx = verts[0], vy = verts[1], vz = verts[2];
    float mnx = vx, mxx = vx, mny = vy, mxy = vy, mnz = vz, mxz = vz;

    size_t count = verts.size() / VERTEX_STRIDE;
    for (size_t i = 1; i < count; ++i)
    {
        float x = verts[i * VERTEX_STRIDE + 0];
        float y = verts[i * VERTEX_STRIDE + 1];
        float z = verts[i * VERTEX_STRIDE + 2];
        if (x < mnx) { mnx = x; } if (x > mxx) { mxx = x; }
        if (y < mny) { mny = y; } if (y > mxy) { mxy = y; }
        if (z < mnz) { mnz = z; } if (z > mxz) { mxz = z; }
    }

    outAABB.min = XMFLOAT3(mnx, mny, mnz);
    outAABB.max = XMFLOAT3(mxx, mxy, mxz);
}

void CullingSystem::UpdateEntityAABB(uint64_t id, const std::vector<float>& verts)
{
    EntityAABB& ea = m_aabbs[id];
    ComputeAABB(verts, ea.localBounds);
    ea.valid = true;
}

bool CullingSystem::IsVisible(const AABB& worldBounds) const
{
    return m_frustum.Intersects(worldBounds);
}

void CullingSystem::CullEntities(const std::vector<uint64_t>& ids,
                                  const std::vector<XMMATRIX>& worldMatrices,
                                  std::vector<uint64_t>& outVisible)
{
    outVisible.clear();
    m_totalCount = (int)ids.size();

    for (size_t i = 0; i < ids.size(); ++i)
    {
        uint64_t id = ids[i];
        auto it = m_aabbs.find(id);
        if (it == m_aabbs.end() || !it->second.valid)
        {
            outVisible.push_back(id);
            continue;
        }

        AABB worldBounds = TransformAABB(it->second.localBounds, worldMatrices[i]);
        if (m_frustum.Intersects(worldBounds))
            outVisible.push_back(id);
    }
    m_visibleCount = (int)outVisible.size();
}

void CullingSystem::RunOcclusionQueries(const std::vector<uint64_t>& ids,
                                         const std::vector<XMMATRIX>& worldMatrices,
                                         const std::vector<uint64_t>& allIds)
{
    if (!m_isD3D11 || !m_initialized) return;

    for (size_t i = 0; i < ids.size(); ++i)
        ResolveQueryResult(ids[i]);

    std::vector<XMMATRIX> matchedWorlds;
    matchedWorlds.reserve(ids.size());
    for (uint64_t id : ids)
    {
        for (size_t j = 0; j < allIds.size(); ++j)
        {
            if (allIds[j] == id)
            {
                matchedWorlds.push_back(worldMatrices[j]);
                break;
            }
        }
    }
    IssueOcclusionQueries(ids, matchedWorlds);
}

// ---------------------------------------------------------------------------
// D3D11 occlusion queries
// ---------------------------------------------------------------------------

void CullingSystem::ResolveQueryResult(uint64_t id)
{
    auto qIt = m_queries.find(id);

    if (qIt == m_queries.end())
    {
        m_queryStates[id].visibleThisFrame = true;
        return;
    }

    ID3D11DeviceContext* ctx = static_cast<ID3D11DeviceContext*>(m_context);
    UINT64 visiblePixelCount = 0;
    HRESULT hr = ctx->GetData(qIt->second, &visiblePixelCount, sizeof(UINT64),
                               D3D11_ASYNC_GETDATA_DONOTFLUSH);

    QueryState& state = m_queryStates[id];
    if (hr == S_OK)
    {
        state.visibleThisFrame = (visiblePixelCount > 0);
        state.visibleLastFrame = state.visibleThisFrame;
    }
    else
    {
        state.visibleThisFrame = state.visibleLastFrame;
    }
}

void CullingSystem::IssueOcclusionQueries(const std::vector<uint64_t>& ids,
                                           const std::vector<XMMATRIX>& worldMatrices)
{
    ID3D11DeviceContext* ctx = static_cast<ID3D11DeviceContext*>(m_context);
    ID3D11Device* dev = static_cast<ID3D11Device*>(m_device);

    ID3D11DepthStencilState* prevDSS = nullptr;
    UINT prevStencilRef = 0;
    ctx->OMGetDepthStencilState(&prevDSS, &prevStencilRef);

    ID3D11RasterizerState* prevRS = nullptr;
    ctx->RSGetState(&prevRS);

    ID3D11VertexShader* prevVS = nullptr;
    ID3D11PixelShader* prevPS = nullptr;
    ctx->VSGetShader(&prevVS, nullptr, nullptr);
    ctx->PSGetShader(&prevPS, nullptr, nullptr);

    ID3D11InputLayout* prevLayout = nullptr;
    ctx->IAGetInputLayout(&prevLayout);

    ctx->OMSetDepthStencilState(m_depthStencilStateNoWrite, 0);
    ctx->RSSetState(m_rasterizerStateProxy);
    ctx->VSSetShader(m_proxyVS, nullptr, 0);
    ctx->PSSetShader(m_proxyPS, nullptr, 0);
    ctx->IASetInputLayout(m_proxyInputLayout);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    UINT stride = sizeof(XMFLOAT3);
    UINT offset = 0;
    ctx->IASetVertexBuffers(0, 1, &m_proxyVB, &stride, &offset);
    ctx->IASetIndexBuffer(m_proxyIB, DXGI_FORMAT_R16_UINT, 0);

    for (size_t i = 0; i < ids.size(); ++i)
    {
        uint64_t id = ids[i];
        auto aIt = m_aabbs.find(id);
        if (aIt == m_aabbs.end() || !aIt->second.valid) continue;

        AABB worldBounds = TransformAABB(aIt->second.localBounds, worldMatrices[i]);

        auto qIt = m_queries.find(id);
        ID3D11Query* query;
        if (qIt != m_queries.end())
        {
            query = qIt->second;
        }
        else
        {
            D3D11_QUERY_DESC qDesc = {};
            qDesc.Query = D3D11_QUERY_OCCLUSION;
            if (FAILED(dev->CreateQuery(&qDesc, &query)))
                continue;
            m_queries[id] = query;
        }

        XMFLOAT3 center = worldBounds.GetCenter();
        XMFLOAT3 extent = worldBounds.GetExtents();

        XMMATRIX scale = XMMatrixScaling(extent.x * 2.0f, extent.y * 2.0f, extent.z * 2.0f);
        XMMATRIX trans = XMMatrixTranslation(center.x, center.y, center.z);
        XMMATRIX wvp = XMMatrixMultiply(XMMatrixMultiply(scale, trans), m_viewProj);

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx->Map(m_proxyCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        {
            XMFLOAT4X4 wvpT;
            XMStoreFloat4x4(&wvpT, XMMatrixTranspose(wvp));
            memcpy(mapped.pData, &wvpT, sizeof(wvpT));
            ctx->Unmap(m_proxyCB, 0);
        }
        ctx->VSSetConstantBuffers(0, 1, &m_proxyCB);

        ctx->Begin(query);
        ctx->DrawIndexed(m_proxyIndexCount, 0, 0);
        ctx->End(query);
    }

    ctx->OMSetDepthStencilState(prevDSS, prevStencilRef);
    ctx->RSSetState(prevRS);
    ctx->VSSetShader(prevVS, nullptr, 0);
    ctx->PSSetShader(prevPS, nullptr, 0);
    ctx->IASetInputLayout(prevLayout);

    SafeRelease(prevDSS);
    SafeRelease(prevRS);
    SafeRelease(prevVS);
    SafeRelease(prevPS);
    SafeRelease(prevLayout);
}

// ---------------------------------------------------------------------------
// D3D11 proxy resource creation
// ---------------------------------------------------------------------------

bool CullingSystem::CreateProxyGeometry()
{
    struct Vertex { XMFLOAT3 pos; };
    Vertex verts[8] = {
        {{-0.5f,-0.5f,-0.5f}}, {{ 0.5f,-0.5f,-0.5f}},
        {{ 0.5f, 0.5f,-0.5f}}, {{-0.5f, 0.5f,-0.5f}},
        {{-0.5f,-0.5f, 0.5f}}, {{ 0.5f,-0.5f, 0.5f}},
        {{ 0.5f, 0.5f, 0.5f}}, {{-0.5f, 0.5f, 0.5f}},
    };
    uint16_t indices[36] = {
        0,1,2, 0,2,3, 4,6,5, 4,7,6,
        0,4,5, 0,5,1, 3,2,6, 3,6,7,
        0,3,7, 0,7,4, 1,5,6, 1,6,2
    };

    ID3D11Device* dev = static_cast<ID3D11Device*>(m_device);

    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.ByteWidth = sizeof(verts);
    vbDesc.Usage = D3D11_USAGE_IMMUTABLE;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vbData = { verts, 0, 0 };
    if (FAILED(dev->CreateBuffer(&vbDesc, &vbData, &m_proxyVB)))
        return false;

    D3D11_BUFFER_DESC ibDesc = {};
    ibDesc.ByteWidth = sizeof(indices);
    ibDesc.Usage = D3D11_USAGE_IMMUTABLE;
    ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA ibData = { indices, 0, 0 };
    if (FAILED(dev->CreateBuffer(&ibDesc, &ibData, &m_proxyIB)))
        return false;

    m_proxyIndexCount = 36;
    return true;
}

bool CullingSystem::CreateProxyShaders()
{
    static const char* vsSrc = R"(
        cbuffer ObjectCB : register(b0) {
            float4x4 worldViewProj;
        };
        float4 main(float3 pos : POSITION) : SV_Position {
            return mul(float4(pos, 1.0f), worldViewProj);
        }
    )";
    static const char* psSrc = R"(
        float4 main() : SV_Target {
            return float4(0,0,0,0);
        }
    )";

    ID3D11Device* dev = static_cast<ID3D11Device*>(m_device);
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errBlob = nullptr;

    HRESULT hr = D3DCompile(vsSrc, strlen(vsSrc), nullptr, nullptr, nullptr,
                             "main", "vs_4_0", 0, 0, &vsBlob, &errBlob);
    if (FAILED(hr)) { SafeRelease(errBlob); return false; }

    hr = D3DCompile(psSrc, strlen(psSrc), nullptr, nullptr, nullptr,
                     "main", "ps_4_0", 0, 0, &psBlob, &errBlob);
    if (FAILED(hr)) { SafeRelease(vsBlob); SafeRelease(errBlob); return false; }

    hr = dev->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                  nullptr, &m_proxyVS);
    if (FAILED(hr)) { SafeRelease(vsBlob); SafeRelease(psBlob); return false; }

    hr = dev->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                 nullptr, &m_proxyPS);
    if (FAILED(hr)) { SafeRelease(vsBlob); SafeRelease(psBlob); return false; }

    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };
    hr = dev->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(),
                                 vsBlob->GetBufferSize(), &m_proxyInputLayout);
    SafeRelease(vsBlob);
    SafeRelease(psBlob);
    if (FAILED(hr)) return false;

    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(XMFLOAT4X4);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev->CreateBuffer(&cbDesc, nullptr, &m_proxyCB)))
        return false;

    return true;
}

bool CullingSystem::CreateStates()
{
    ID3D11Device* dev = static_cast<ID3D11Device*>(m_device);

    D3D11_DEPTH_STENCIL_DESC dsDesc = {};
    dsDesc.DepthEnable = TRUE;
    dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (FAILED(dev->CreateDepthStencilState(&dsDesc, &m_depthStencilStateNoWrite)))
        return false;

    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE;
    rsDesc.DepthClipEnable = TRUE;
    if (FAILED(dev->CreateRasterizerState(&rsDesc, &m_rasterizerStateProxy)))
        return false;

    return true;
}

#ifdef EDITOR_BUILD
// ── Viewport picking ──

static bool RayIntersectsAABB(const XMFLOAT3& ro, const XMFLOAT3& rd,
                               const XMFLOAT3& aabbMin, const XMFLOAT3& aabbMax,
                               float& outT)
{
    float tmin = -std::numeric_limits<float>::max();
    float tmax = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis)
    {
        float o = (&ro.x)[axis];
        float d = (&rd.x)[axis];
        float min = (&aabbMin.x)[axis];
        float max = (&aabbMax.x)[axis];

        if (std::fabs(d) < 1e-10f)
        {
            if (o < min || o > max) return false;
        }
        else
        {
            float invD = 1.0f / d;
            float t1 = (min - o) * invD;
            float t2 = (max - o) * invD;
            if (t1 > t2) std::swap(t1, t2);
            if (t1 > tmin) tmin = t1;
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return false;
        }
    }
    outT = tmin;
    return true;
}

static void ComputeInvVP(const XMMATRIX& view, const XMMATRIX& proj, float invVP[16])
{
    XMMATRIX vp = view * proj;
    XMVECTOR det;
    XMMATRIX inv = XMMatrixInverse(&det, vp);
    memcpy(invVP, &inv, sizeof(float) * 16);
}

static void ScreenToWorldRay(const float invVP[16],
                              float sx, float sy, int vpW, int vpH,
                              XMFLOAT3& origin, XMFLOAT3& dir)
{
    float nx = sx / vpW * 2.0f - 1.0f;
    float ny = 1.0f - sy / vpH * 2.0f;

    float nearP[4] = { nx, ny, 0.0f, 1.0f };
    float farP[4]  = { nx, ny, 1.0f, 1.0f };
    float nearW[4], farW[4];

    auto matMul = [](const float m[16], const float v[4], float out[4]) {
        out[0] = m[0]*v[0] + m[4]*v[1] + m[8]*v[2] + m[12]*v[3];
        out[1] = m[1]*v[0] + m[5]*v[1] + m[9]*v[2] + m[13]*v[3];
        out[2] = m[2]*v[0] + m[6]*v[1] + m[10]*v[2] + m[14]*v[3];
        out[3] = m[3]*v[0] + m[7]*v[1] + m[11]*v[2] + m[15]*v[3];
    };

    matMul(invVP, nearP, nearW);
    matMul(invVP, farP, farW);

    if (std::fabs(nearW[3]) < FLT_EPSILON || std::fabs(farW[3]) < FLT_EPSILON)
    {
        origin = XMFLOAT3(0, 0, 0);
        dir = XMFLOAT3(0, 0, 1);
        return;
    }
    float inw = 1.0f / nearW[3], ifw = 1.0f / farW[3];
    origin = XMFLOAT3(nearW[0]*inw, nearW[1]*inw, nearW[2]*inw);
    float fx = farW[0]*ifw, fy = farW[1]*ifw, fz = farW[2]*ifw;
    float dx = fx - origin.x, dy = fy - origin.y, dz = fz - origin.z;
    float len = std::sqrt(dx*dx + dy*dy + dz*dz);
    if (len > 1e-10f) { dx /= len; dy /= len; dz /= len; }
    dir = XMFLOAT3(dx, dy, dz);
}

uint64_t CullingSystem::PickEntity(const std::vector<Entity>& entities,
                                    float screenX, float screenY,
                                    int viewportW, int viewportH,
                                    const XMMATRIX& view, const XMMATRIX& proj)
{
    if (viewportW <= 0 || viewportH <= 0) return 0;

    float invVP[16];
    ComputeInvVP(view, proj, invVP);

    XMFLOAT3 rayOrigin, rayDir;
    ScreenToWorldRay(invVP, screenX, screenY, viewportW, viewportH,
                     rayOrigin, rayDir);

    uint64_t closestId = 0;
    float closestT = std::numeric_limits<float>::max();

    for (const auto& entity : entities)
    {
        if (entity.vertices.empty()) continue;
        if (entity.HasFlag(ENTITY_SERVER_SERVICE) || entity.HasFlag(ENTITY_SCRIPT)) continue;

        auto it = m_aabbs.find(entity.id);
        if (it == m_aabbs.end() || !it->second.valid) continue;

        XMMATRIX world = ComputeWorldMatrix(entity.transform);
        AABB worldBounds = TransformAABB(it->second.localBounds, world);

        float t;
        if (RayIntersectsAABB(rayOrigin, rayDir,
                              worldBounds.min, worldBounds.max, t))
        {
            if (t >= 0 && t < closestT)
            {
                closestT = t;
                closestId = entity.id;
            }
        }
    }
    return closestId;
}
#endif // EDITOR_BUILD
