#include "4xPhys.h"
#include "ecs/ECS.h"
#include <btBulletDynamicsCommon.h>
#include "BulletCollision/CollisionShapes/btShapeHull.h"
#include <unordered_set>
#include <cstdio>

using namespace DirectX;
using namespace phy;

PhysWorld4X::PhysWorld4X()
{
    m_CollisionConfig = new btDefaultCollisionConfiguration();
    m_Dispatcher = new btCollisionDispatcher(m_CollisionConfig);
    m_Broadphase = new btDbvtBroadphase();
    m_Solver = new btSequentialImpulseConstraintSolver();
    m_DynamicsWorld = new btDiscreteDynamicsWorld(m_Dispatcher, m_Broadphase, m_Solver, m_CollisionConfig);
    m_DynamicsWorld->setGravity(btVector3(0, -9.81f, 0));
}

PhysWorld4X::~PhysWorld4X()
{
    for (auto& pair : m_Bodies)
        destroyBody(pair.first);
    m_Bodies.clear();
    delete m_DynamicsWorld;
    delete m_Solver;
    delete m_Broadphase;
    delete m_Dispatcher;
    delete m_CollisionConfig;
}

btCollisionShape* PhysWorld4X::createShape(const std::vector<float>& verts)
{
    if (verts.empty())
        return nullptr;

    if (verts.size() < 9) {
        return new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));
    }

    // Use a convex hull for imported meshes or large vertex counts — gives
    // a much better fit than the old AABB-box approach.
    btConvexHullShape* hull = new btConvexHullShape();
    int vertCount = (int)verts.size() / 9;
    for (int i = 0; i < vertCount; i++) {
        float x = verts[i * 9 + 0];
        float y = verts[i * 9 + 1];
        float z = verts[i * 9 + 2];
        hull->addPoint(btVector3(x, y, z), false);
    }
    hull->recalcLocalAabb();
    hull->setMargin(0.04f);

    // Ensure minimum thickness so degenerate meshes still generate contacts
    btVector3 aabbMin, aabbMax;
    hull->getAabb(btTransform::getIdentity(), aabbMin, aabbMax);
    btVector3 ext = aabbMax - aabbMin;
    const float minExt = 0.02f;
    if (ext.x() < minExt || ext.y() < minExt || ext.z() < minExt) {
        delete hull;
        float sx = std::max(ext.x() * 0.5f, minExt * 0.5f);
        float sy = std::max(ext.y() * 0.5f, minExt * 0.5f);
        float sz = std::max(ext.z() * 0.5f, minExt * 0.5f);
        btBoxShape* box = new btBoxShape(btVector3(sx, sy, sz));
        box->setMargin(0.04f);
        return box;
    }

    return hull;
}

void PhysWorld4X::BuildCollisionMesh(const std::vector<float>& src, std::vector<float>& out, const char* savePath)
{
    out.clear();
    if (src.size() < 9) return;

    int vertCount = (int)src.size() / 9;
    btConvexHullShape* hull = new btConvexHullShape();
    for (int i = 0; i < vertCount; i++) {
        hull->addPoint(btVector3(src[i * 9], src[i * 9 + 1], src[i * 9 + 2]), false);
    }
    hull->recalcLocalAabb();
    hull->setMargin(0.04f);

    // Simplify using btShapeHull (approximates with 42 support directions)
    btShapeHull* hullMesh = new btShapeHull(hull);
    hullMesh->buildHull(hull->getMargin());

    int numVerts = hullMesh->numVertices();
    out.reserve(numVerts * 9);
    for (int i = 0; i < numVerts; i++) {
        const btVector3& p = hullMesh->getVertexPointer()[i];
        out.push_back(p.x()); out.push_back(p.y()); out.push_back(p.z());
        out.push_back(0.0f); out.push_back(0.0f); out.push_back(0.0f); // normal (unused)
        out.push_back(0.0f); out.push_back(0.0f); out.push_back(0.0f); // color (unused)
    }

    // Save raw hull positions to .4xc file if path provided
    if (savePath) {
        std::string hullPath(savePath);
        size_t dot = hullPath.rfind('.');
        if (dot != std::string::npos)
            hullPath.resize(dot);
        hullPath += ".4xc";

        FILE* f = fopen(hullPath.c_str(), "wb");
        if (f) {
            uint32_t n = numVerts;
            fwrite(&n, sizeof(n), 1, f);
            for (int i = 0; i < numVerts; i++) {
                const btVector3& p = hullMesh->getVertexPointer()[i];
                float v[3] = { p.x(), p.y(), p.z() };
                fwrite(v, sizeof(v), 1, f);
            }
            fclose(f);
        }
    }

    delete hullMesh;
    delete hull;
}

void PhysWorld4X::createBody(Entity& e)
{
    BulletBody bb;
    const auto& shapeVerts = e.collisionVertices.empty() ? e.vertices : e.collisionVertices;
    bb.shape = createShape(shapeVerts);
    if (!bb.shape)
        bb.shape = new btBoxShape(btVector3(0.5f, 0.5f, 0.5f));

    bb.shape->setLocalScaling(btVector3(
        e.transform.scale.x,
        e.transform.scale.y,
        e.transform.scale.z));

    bool isStatic = e.physics.IsStatic();
    btVector3 inertia(0, 0, 0);
    float mass = isStatic ? 0.0f : e.physics.mass;
    if (mass > 0)
        bb.shape->calculateLocalInertia(mass, inertia);

    btTransform t;
    t.setOrigin(btVector3(e.transform.position.x, e.transform.position.y, e.transform.position.z));
    btMatrix3x3 mat;
    mat.setEulerYPR(e.transform.rotation.y, e.transform.rotation.x, e.transform.rotation.z);
    t.setBasis(mat);

    btDefaultMotionState* motionState = new btDefaultMotionState(t);
    btRigidBody::btRigidBodyConstructionInfo ci(mass, motionState, bb.shape, inertia);
    bb.body = new btRigidBody(ci);
    bb.body->setRestitution(e.physics.restitution);
    bb.body->setFriction(e.physics.friction);
    bb.body->setRollingFriction(e.physics.friction * 0.1f);
    bb.body->setLinearVelocity(btVector3(e.physics.velocity.x, e.physics.velocity.y, e.physics.velocity.z));
    bb.body->setDamping(0.005f, 0.95f);
    bb.body->setUserPointer(&e);

    int cf = 0;
    if (isStatic)
        cf |= btCollisionObject::CF_STATIC_OBJECT;
    else
        cf |= btCollisionObject::CF_DYNAMIC_OBJECT;
    if (!e.physics.collidable)
        cf |= btCollisionObject::CF_NO_CONTACT_RESPONSE;
    bb.body->setCollisionFlags(cf);

    m_DynamicsWorld->addRigidBody(bb.body);
    m_Bodies[e.id] = bb;
}

void PhysWorld4X::destroyBody(uint64_t id)
{
    auto it = m_Bodies.find(id);
    if (it == m_Bodies.end()) return;
    btRigidBody* body = it->second.body;
    btCollisionShape* shape = it->second.shape;
    if (body) {
        m_DynamicsWorld->removeRigidBody(body);
        delete body->getMotionState();
        delete body;
    }
    delete shape;
    m_Bodies.erase(it);
}

void PhysWorld4X::syncToBullet(Entity& e, btRigidBody* body)
{
    btTransform t;
    t.setOrigin(btVector3(e.transform.position.x, e.transform.position.y, e.transform.position.z));
    btMatrix3x3 mat;
    mat.setEulerYPR(e.transform.rotation.y, e.transform.rotation.x, e.transform.rotation.z);
    t.setBasis(mat);
    body->setWorldTransform(t);
    body->getMotionState()->setWorldTransform(t);
    body->setLinearVelocity(btVector3(e.physics.velocity.x, e.physics.velocity.y, e.physics.velocity.z));
    body->activate();
}

void PhysWorld4X::syncFromBullet(Entity& e, btRigidBody* body)
{
    btTransform t;
    body->getMotionState()->getWorldTransform(t);
    btVector3 origin = t.getOrigin();
    e.transform.position.x = origin.x();
    e.transform.position.y = origin.y();
    e.transform.position.z = origin.z();

    btQuaternion q = t.getRotation();
    btMatrix3x3 mat(q);
    float yaw, pitch, roll;
    mat.getEulerYPR(yaw, pitch, roll);
    e.transform.rotation.x = pitch;
    e.transform.rotation.y = yaw;
    e.transform.rotation.z = roll;

    btVector3 vel = body->getLinearVelocity();
    e.physics.velocity.x = vel.x();
    e.physics.velocity.y = vel.y();
    e.physics.velocity.z = vel.z();
}

void PhysWorld4X::Tick(float dt, std::vector<Entity>& entities)
{
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;

    std::unordered_set<uint64_t> activeIds;

    for (auto& e : entities) {
        if (!e.physics.enabled && !e.physics.collidable) continue;
        activeIds.insert(e.id);

        auto it = m_Bodies.find(e.id);
        if (it == m_Bodies.end()) {
            createBody(e);
            it = m_Bodies.find(e.id);
            if (it == m_Bodies.end()) continue;
        }

        BulletBody& bb = it->second;
        btRigidBody* body = bb.body;

        bool isStatic = e.physics.IsStatic();
        bool bodyIsStatic = (body->getInvMass() == 0.0f);

        if (isStatic != bodyIsStatic) {
            destroyBody(e.id);
            createBody(e);
            continue;
        }

        body->setRestitution(e.physics.restitution);
        body->setFriction(e.physics.friction);
        body->setRollingFriction(e.physics.friction * 0.1f);

        btVector3 scale(e.transform.scale.x, e.transform.scale.y, e.transform.scale.z);
        if (bb.shape->getLocalScaling() != scale)
            bb.shape->setLocalScaling(scale);

        if (!isStatic) {
            btVector3 inertia;
            bb.shape->calculateLocalInertia(e.physics.mass, inertia);
            body->setMassProps(e.physics.mass, inertia);
            syncToBullet(e, body);
        } else {
            btTransform t;
            t.setOrigin(btVector3(e.transform.position.x, e.transform.position.y, e.transform.position.z));
            btMatrix3x3 mat;
            mat.setEulerYPR(e.transform.rotation.y, e.transform.rotation.x, e.transform.rotation.z);
            t.setBasis(mat);
            body->setWorldTransform(t);
            body->getMotionState()->setWorldTransform(t);
        }
    }

    std::vector<uint64_t> toRemove;
    for (auto& pair : m_Bodies) {
        if (activeIds.find(pair.first) == activeIds.end())
            toRemove.push_back(pair.first);
    }
    for (uint64_t id : toRemove)
        destroyBody(id);

    m_DynamicsWorld->stepSimulation(dt, 4, 1.0f / 120.0f);

    for (auto& e : entities) {
        auto it = m_Bodies.find(e.id);
        if (it == m_Bodies.end() || !it->second.body) continue;
        if (!e.physics.enabled) continue;
        btRigidBody* body = it->second.body;
        if (!body) continue;
        if (body->getInvMass() == 0.0f) continue;
        syncFromBullet(e, body);
    }
}
