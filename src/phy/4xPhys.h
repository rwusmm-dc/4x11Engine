#pragma once
#include <DirectXMath.h>
#include <vector>
#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>
#include <unordered_map>

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

} // namespace phy

class Entity;
class btDiscreteDynamicsWorld;
class btBroadphaseInterface;
class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btSequentialImpulseConstraintSolver;
class btRigidBody;
class btCollisionShape;

class PhysWorld4X {
public:
    PhysWorld4X();
    ~PhysWorld4X();

    void Tick(float dt, std::vector<Entity>& entities);

private:
    struct BulletBody {
        btRigidBody* body = nullptr;
        btCollisionShape* shape = nullptr;
    };

    btDiscreteDynamicsWorld*         m_DynamicsWorld;
    btBroadphaseInterface*           m_Broadphase;
    btDefaultCollisionConfiguration* m_CollisionConfig;
    btCollisionDispatcher*           m_Dispatcher;
    btSequentialImpulseConstraintSolver* m_Solver;
    std::unordered_map<uint64_t, BulletBody> m_Bodies;

    void createBody(Entity& e);
    void destroyBody(uint64_t id);
    void syncToBullet(Entity& e, btRigidBody* body);
    void syncFromBullet(Entity& e, btRigidBody* body);
    btCollisionShape* createShape(const std::vector<float>& verts);

public:
    // Build a convex hull collision mesh from visual mesh vertices (stride 9 format).
    // Outputs hull vertices (stride 9) into outCollision, and optionally saves raw positions to a .4xc file.
    static void BuildCollisionMesh(const std::vector<float>& srcVertices,
                                   std::vector<float>& outCollision,
                                   const char* savePath = nullptr);
};
