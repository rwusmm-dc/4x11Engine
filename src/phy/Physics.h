#pragma once
#include <DirectXMath.h>
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>

namespace phy {

struct AABB {
    DirectX::XMFLOAT3 min = { FLT_MAX, FLT_MAX, FLT_MAX };
    DirectX::XMFLOAT3 max = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

    bool IsValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
};

struct BoundingSphere {
    DirectX::XMFLOAT3 center = { 0, 0, 0 };
    float radius = 0.0f;
};

struct PhysicsComponent {
    bool enabled = false;
    bool collidable = true;
    float mass = 1.0f;
    float restitution = 0.2f;
    float friction = 0.4f;
    DirectX::XMFLOAT3 velocity = { 0, 0, 0 };

    bool IsStatic() const { return !enabled || mass <= 0.0f; }
    float InvMass() const { return IsStatic() ? 0.0f : 1.0f / mass; }
};

AABB ComputeAABBFromVerts(const std::vector<float>& verts, size_t stride = 9, size_t offset = 0);
AABB TransformAABB(const AABB& local, const DirectX::XMMATRIX& world);
DirectX::XMFLOAT3 AABBCenter(const AABB& aabb);

} // namespace phy

class Entity;

class PhysicsWorld {
public:
    static constexpr float GRAVITY     = -9.81f;
    static constexpr int   ITERATIONS  = 8;
    static constexpr float VEL_LIMIT   = 50.0f;
    static constexpr float CORRECTION  = 1.0f;
    static constexpr float SLOP        = 0.0f;
    static constexpr int   SUB_STEPS   = 4;
    static constexpr float RESTING_VEL = 0.2f;
    static constexpr float DAMPING     = 0.998f;

    struct Manifold {
        Entity* a = nullptr;
        Entity* b = nullptr;
        DirectX::XMFLOAT3 normal = { 0, 1, 0 };
        float penetration = 0.0f;
    };

    void Tick(float dt, std::vector<Entity>& entities);

private:
    struct Body {
        Entity* e = nullptr;
        phy::AABB localAabb;   // cached local-space AABB (mesh space, doesn't change per substep)
        phy::AABB waabb;       // recomputed fresh each substep from localAabb + current world transform
        float invMass = 0.0f;
        bool isStatic = false;
        bool hasGeometry = false; // false => no valid local AABB (empty verts); excluded from narrow phase
    };

    void fillBodies(std::vector<Entity>& entities, std::vector<Body>& bodies);
    void refreshWorldAABBs(std::vector<Body>& bodies); // recompute waabb from current transform, post-integration
    void sweepPrune(std::vector<Body>& bodies, std::vector<std::pair<size_t,size_t>>& pairs);
    bool aabbOverlap(const phy::AABB& a, const phy::AABB& b);
    bool buildManifold(const Body& a, const Body& b, Manifold& m);
    void resolve(Manifold& m, float dt);
    void posCorrect(Manifold& m);
};