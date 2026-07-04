#pragma once
#include <d3d11.h>
#include <DirectXMath.h>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct Entity;

struct AABB {
    DirectX::XMFLOAT3 min;
    DirectX::XMFLOAT3 max;

    DirectX::XMFLOAT3 GetCenter() const;
    DirectX::XMFLOAT3 GetExtents() const;
};

struct Plane {
    DirectX::XMFLOAT4 v;
    float DistanceToPoint(const DirectX::XMFLOAT3& p) const;
};

struct Frustum {
    Plane planes[6];
    enum PlaneIndex { Left = 0, Right, Top, Bottom, Near, Far };

    void ExtractFromMatrix(const DirectX::XMMATRIX& viewProj);
    bool Intersects(const AABB& box) const;
};

class CullingSystem {
public:
    CullingSystem() = default;
    ~CullingSystem() { Shutdown(); }

    void Init(void* device, void* context, bool isD3D11);
    void Shutdown();

    void BeginFrame(const DirectX::XMMATRIX& viewProj);

    void ComputeAABB(const std::vector<float>& verts, AABB& outAABB);

    void UpdateEntityAABB(uint64_t id, const std::vector<float>& verts);

    bool IsVisible(const AABB& worldBounds) const;

    void CullEntities(const std::vector<uint64_t>& ids,
                      const std::vector<DirectX::XMMATRIX>& worldMatrices,
                      std::vector<uint64_t>& outVisible);

    void RunOcclusionQueries(const std::vector<uint64_t>& ids,
                             const std::vector<DirectX::XMMATRIX>& worldMatrices,
                             const std::vector<uint64_t>& allIds);

#ifdef EDITOR_BUILD
    uint64_t PickEntity(const std::vector<Entity>& entities, float screenX, float screenY,
                         int viewportW, int viewportH,
                         const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj);
#endif

    int GetVisibleCount() const { return m_visibleCount; }
    int GetTotalCount() const { return m_totalCount; }

private:
    struct EntityAABB {
        AABB localBounds;
        bool valid = false;
    };

    void ResolveQueryResult(uint64_t id);
    void IssueOcclusionQueries(const std::vector<uint64_t>& ids,
                               const std::vector<DirectX::XMMATRIX>& worldMatrices);

    template <typename T>
    static void SafeRelease(T*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    }

    bool CreateProxyGeometry();
    bool CreateProxyShaders();
    bool CreateStates();

    void* m_device = nullptr;
    void* m_context = nullptr;
    bool m_isD3D11 = false;
    bool m_initialized = false;

    Frustum m_frustum;
    DirectX::XMMATRIX m_viewProj = DirectX::XMMatrixIdentity();

    std::unordered_map<uint64_t, EntityAABB> m_aabbs;

    int m_visibleCount = 0;
    int m_totalCount = 0;

    ID3D11Buffer* m_proxyVB = nullptr;
    ID3D11Buffer* m_proxyIB = nullptr;
    ID3D11Buffer* m_proxyCB = nullptr;
    ID3D11VertexShader* m_proxyVS = nullptr;
    ID3D11PixelShader* m_proxyPS = nullptr;
    ID3D11InputLayout* m_proxyInputLayout = nullptr;
    UINT m_proxyIndexCount = 0;

    ID3D11DepthStencilState* m_depthStencilStateNoWrite = nullptr;
    ID3D11RasterizerState* m_rasterizerStateProxy = nullptr;

    struct QueryState {
        bool visibleLastFrame = true;
        bool visibleThisFrame = true;
    };
    std::unordered_map<uint64_t, ID3D11Query*> m_queries;
    std::unordered_map<uint64_t, QueryState> m_queryStates;
};
