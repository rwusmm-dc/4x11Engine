#include "phy/Physics.h"
#include "ecs/ECS.h"

using namespace DirectX;
using namespace phy;

namespace phy {

AABB ComputeAABBFromVerts(const std::vector<float>& verts, size_t stride, size_t offset) {
    AABB aabb;
    for (size_t i = offset; i + 2 < verts.size(); i += stride) {
        float x = verts[i], y = verts[i+1], z = verts[i+2];
        if (x < aabb.min.x) aabb.min.x = x;
        if (y < aabb.min.y) aabb.min.y = y;
        if (z < aabb.min.z) aabb.min.z = z;
        if (x > aabb.max.x) aabb.max.x = x;
        if (y > aabb.max.y) aabb.max.y = y;
        if (z > aabb.max.z) aabb.max.z = z;
    }
    return aabb;
}

AABB TransformAABB(const AABB& local, const XMMATRIX& world) {
    XMFLOAT3 corners[8] = {
        {local.min.x, local.min.y, local.min.z},
        {local.max.x, local.min.y, local.min.z},
        {local.min.x, local.max.y, local.min.z},
        {local.max.x, local.max.y, local.min.z},
        {local.min.x, local.min.y, local.max.z},
        {local.max.x, local.min.y, local.max.z},
        {local.min.x, local.max.y, local.max.z},
        {local.max.x, local.max.y, local.max.z},
    };
    AABB out;
    for (int i = 0; i < 8; i++) {
        XMVECTOR p = XMVector3Transform(XMLoadFloat3(&corners[i]), world);
        XMFLOAT3 wp;
        XMStoreFloat3(&wp, p);
        if (wp.x < out.min.x) out.min.x = wp.x;
        if (wp.y < out.min.y) out.min.y = wp.y;
        if (wp.z < out.min.z) out.min.z = wp.z;
        if (wp.x > out.max.x) out.max.x = wp.x;
        if (wp.y > out.max.y) out.max.y = wp.y;
        if (wp.z > out.max.z) out.max.z = wp.z;
    }
    return out;
}

XMFLOAT3 AABBCenter(const AABB& aabb) {
    return {
        (aabb.min.x + aabb.max.x) * 0.5f,
        (aabb.min.y + aabb.max.y) * 0.5f,
        (aabb.min.z + aabb.max.z) * 0.5f,
    };
}

} // namespace phy

void PhysicsWorld::fillBodies(std::vector<Entity>& entities, std::vector<Body>& bodies) {
    for (auto& e : entities) {
        if (!e.physics.enabled && !e.physics.collidable) continue;

        Body b;
        b.e = &e;

        if (e.vertices.empty()) {
            // No mesh geometry: can't build a meaningful AABB. Keep the body out
            // of narrow-phase collision entirely rather than letting a degenerate
            // (inverted) AABB silently participate in sweep/prune.
            b.hasGeometry = false;
            bool isStatic = !e.physics.enabled || e.physics.IsStatic();
            b.isStatic = isStatic;
            b.invMass = isStatic ? 0.0f : 1.0f / e.physics.mass;
            bodies.push_back(b);
            continue;
        }

        b.localAabb = ComputeAABBFromVerts(e.vertices);
        if (!b.localAabb.IsValid()) {
            // Degenerate mesh (e.g. fewer than 3 verts worth of data). Same treatment.
            b.hasGeometry = false;
            bool isStatic = !e.physics.enabled || e.physics.IsStatic();
            b.isStatic = isStatic;
            b.invMass = isStatic ? 0.0f : 1.0f / e.physics.mass;
            bodies.push_back(b);
            continue;
        }

        b.hasGeometry = true;
        XMMATRIX world = ComputeWorldMatrix(e.transform);
        b.waabb = TransformAABB(b.localAabb, world);
        bool isStatic = !e.physics.enabled || e.physics.IsStatic();
        b.isStatic = isStatic;
        b.invMass = isStatic ? 0.0f : 1.0f / e.physics.mass;
        bodies.push_back(b);
    }
}

void PhysicsWorld::refreshWorldAABBs(std::vector<Body>& bodies) {
    // Recompute each dynamic body's world AABB directly from its (now-updated)
    // transform, instead of translating the stale AABB by velocity*dt. This is
    // the correct approach because rotation/scale changes during the substep
    // (or accumulated error from repeated shifting) would otherwise desync the
    // AABB from the actual transformed mesh, causing missed/incorrect collisions.
    for (auto& b : bodies) {
        if (b.isStatic || !b.hasGeometry) continue;
        XMMATRIX world = ComputeWorldMatrix(b.e->transform);
        b.waabb = TransformAABB(b.localAabb, world);
    }
}

void PhysicsWorld::sweepPrune(std::vector<Body>& bodies,
                              std::vector<std::pair<size_t,size_t>>& pairs) {
    std::vector<size_t> indices;
    indices.reserve(bodies.size());
    for (size_t i = 0; i < bodies.size(); i++) {
        if (!bodies[i].hasGeometry) continue; // exclude geometry-less bodies from the sweep
        indices.push_back(i);
    }
    std::sort(indices.begin(), indices.end(),
        [&](size_t a, size_t b) { return bodies[a].waabb.min.x < bodies[b].waabb.min.x; });

    for (size_t i = 0; i < indices.size(); i++) {
        size_t ai = indices[i];
        for (size_t j = i + 1; j < indices.size(); j++) {
            size_t bi = indices[j];
            if (bodies[bi].waabb.min.x > bodies[ai].waabb.max.x) break;
            // Skip static-static pairs; they can never produce a meaningful response
            // and are cheap to filter here before narrow phase.
            if (bodies[ai].isStatic && bodies[bi].isStatic) continue;
            pairs.push_back({ai, bi});
        }
    }
}

bool PhysicsWorld::aabbOverlap(const phy::AABB& a, const phy::AABB& b) {
    if (a.max.x < b.min.x || a.min.x > b.max.x) return false;
    if (a.max.y < b.min.y || a.min.y > b.max.y) return false;
    if (a.max.z < b.min.z || a.min.z > b.max.z) return false;
    return true;
}

bool PhysicsWorld::buildManifold(const Body& ba, const Body& bb, Manifold& m) {
    const phy::AABB& a = ba.waabb;
    const phy::AABB& b = bb.waabb;

    float overlap[3] = {
        std::min(a.max.x - b.min.x, b.max.x - a.min.x),
        std::min(a.max.y - b.min.y, b.max.y - a.min.y),
        std::min(a.max.z - b.min.z, b.max.z - a.min.z),
    };

    // Pick the axis of least penetration with a single consistent comparator
    // (previously mixed < / <= across axis 0/1/2, which made the outcome
    // depend on iteration order on exact ties and could skip axis 2 vs axis 0
    // entirely). This scans all three uniformly.
    int axis = 0;
    for (int i = 1; i < 3; i++) {
        if (overlap[i] < overlap[axis]) axis = i;
    }

    if (overlap[axis] < 1e-6f) return false;

    m.a = ba.e;
    m.b = bb.e;
    m.penetration = overlap[axis];
    static const float axes[3][3] = {
        {1,0,0}, {0,1,0}, {0,0,1}
    };

    // Determine push-out direction along the chosen axis.
    // Normal points from B toward A (A is pushed along +normal).
    float ca = (&a.min.x)[axis], cb = (&b.min.x)[axis];
    float da = (&a.max.x)[axis], db = (&b.max.x)[axis];
    float centerA = (ca + da) * 0.5f;
    float centerB = (cb + db) * 0.5f;
    // Stable sign: purely based on relative box centers along the axis.
    // Falls back to +1 only in the exact-tie case (centers coincide), which
    // is an arbitrary but *consistent* choice — it no longer flip-flops
    // between frames since it doesn't depend on min/max edge comparisons.
    float sign = (centerA >= centerB) ? 1.0f : -1.0f;

    m.normal.x = axes[axis][0] * sign;
    m.normal.y = axes[axis][1] * sign;
    m.normal.z = axes[axis][2] * sign;

    return true;
}

void PhysicsWorld::resolve(Manifold& m, float dt) {
    Entity* a = m.a;
    Entity* b = m.b;
    float invMassA = a->physics.IsStatic() ? 0.0f : 1.0f / a->physics.mass;
    float invMassB = b->physics.IsStatic() ? 0.0f : 1.0f / b->physics.mass;
    float totalInv = invMassA + invMassB;
    if (totalInv < 1e-10f) return;

    XMVECTOR n = XMLoadFloat3(&m.normal);
    XMVECTOR va = XMLoadFloat3(&a->physics.velocity);
    XMVECTOR vb = XMLoadFloat3(&b->physics.velocity);
    XMVECTOR rel = va - vb;
    float relDotN;
    XMStoreFloat(&relDotN, XMVector3Dot(rel, n));

    if (relDotN > 0.0f) return;

    float e = std::min(a->physics.restitution, b->physics.restitution);
    if (relDotN > -RESTING_VEL) {
        e = 0.0f;
    }
    float jn = -(1.0f + e) * relDotN / totalInv;

    a->physics.velocity.x += jn * invMassA * m.normal.x;
    a->physics.velocity.y += jn * invMassA * m.normal.y;
    a->physics.velocity.z += jn * invMassA * m.normal.z;
    b->physics.velocity.x -= jn * invMassB * m.normal.x;
    b->physics.velocity.y -= jn * invMassB * m.normal.y;
    b->physics.velocity.z -= jn * invMassB * m.normal.z;

    float mu = std::sqrt(a->physics.friction * b->physics.friction);
    {
        XMVECTOR tangent = rel - n * relDotN;
        float tLen;
        XMStoreFloat(&tLen, XMVector3Length(tangent));
        if (tLen > 1e-6f) {
            XMVECTOR tDir = tangent / tLen;
            float relDotT;
            XMStoreFloat(&relDotT, XMVector3Dot(rel, tDir));
            float jt = -relDotT / totalInv;
            if (std::fabs(jt) > jn * mu) jt = (jt > 0 ? 1 : -1) * jn * mu;
            XMFLOAT3 tDirF;
            XMStoreFloat3(&tDirF, tDir);
            a->physics.velocity.x += jt * invMassA * tDirF.x;
            a->physics.velocity.y += jt * invMassA * tDirF.y;
            a->physics.velocity.z += jt * invMassA * tDirF.z;
            b->physics.velocity.x -= jt * invMassB * tDirF.x;
            b->physics.velocity.y -= jt * invMassB * tDirF.y;
            b->physics.velocity.z -= jt * invMassB * tDirF.z;
        }
    }

    XMVECTOR newVA = XMLoadFloat3(&a->physics.velocity);
    XMVECTOR newVB = XMLoadFloat3(&b->physics.velocity);
    XMVECTOR newRel = newVA - newVB;
    float newRelDotN;
    XMStoreFloat(&newRelDotN, XMVector3Dot(newRel, n));
    if (std::fabs(newRelDotN) < RESTING_VEL) {
        float corr = newRelDotN / totalInv;
        a->physics.velocity.x -= corr * invMassA * m.normal.x;
        a->physics.velocity.y -= corr * invMassA * m.normal.y;
        a->physics.velocity.z -= corr * invMassA * m.normal.z;
        b->physics.velocity.x += corr * invMassB * m.normal.x;
        b->physics.velocity.y += corr * invMassB * m.normal.y;
        b->physics.velocity.z += corr * invMassB * m.normal.z;
    }
}

void PhysicsWorld::posCorrect(Manifold& m) {
    Entity* a = m.a;
    Entity* b = m.b;
    float invMassA = a->physics.IsStatic() ? 0.0f : 1.0f / a->physics.mass;
    float invMassB = b->physics.IsStatic() ? 0.0f : 1.0f / b->physics.mass;
    float totalInv = invMassA + invMassB;
    if (totalInv < 1e-10f) return;

    float pen = std::max(m.penetration - SLOP, 0.0f);
    float corr = pen * CORRECTION / totalInv;

    a->transform.position.x += corr * invMassA * m.normal.x;
    a->transform.position.y += corr * invMassA * m.normal.y;
    a->transform.position.z += corr * invMassA * m.normal.z;
    b->transform.position.x -= corr * invMassB * m.normal.x;
    b->transform.position.y -= corr * invMassB * m.normal.y;
    b->transform.position.z -= corr * invMassB * m.normal.z;

    float corrected = corr * totalInv;
    m.penetration -= corrected;
    if (m.penetration < 0.0f) m.penetration = 0.0f;
}

void PhysicsWorld::Tick(float dt, std::vector<Entity>& entities) {
    if (dt <= 0.0f) return;
    if (dt > 0.05f) dt = 0.05f;

    float stepDt = dt / (float)SUB_STEPS;

    for (int step = 0; step < SUB_STEPS; step++) {
        std::vector<Body> bodies;
        fillBodies(entities, bodies);
        if (bodies.empty()) continue;

        // Integrate velocity + position for dynamic bodies.
        for (auto& b : bodies) {
            if (b.isStatic) continue;
            if (!b.e->physics.enabled) continue;
            b.e->physics.velocity.y += GRAVITY * stepDt;
            float spd = std::sqrt(
                b.e->physics.velocity.x * b.e->physics.velocity.x +
                b.e->physics.velocity.y * b.e->physics.velocity.y +
                b.e->physics.velocity.z * b.e->physics.velocity.z);
            if (spd > VEL_LIMIT) {
                b.e->physics.velocity.x = b.e->physics.velocity.x / spd * VEL_LIMIT;
                b.e->physics.velocity.y = b.e->physics.velocity.y / spd * VEL_LIMIT;
                b.e->physics.velocity.z = b.e->physics.velocity.z / spd * VEL_LIMIT;
            }
            b.e->transform.position.x += b.e->physics.velocity.x * stepDt;
            b.e->transform.position.y += b.e->physics.velocity.y * stepDt;
            b.e->transform.position.z += b.e->physics.velocity.z * stepDt;
        }

        // Recompute world AABBs from the now-current transform (correct),
        // instead of translating stale AABBs by velocity*dt (previous bug:
        // desyncs under rotation and accumulates error).
        refreshWorldAABBs(bodies);

        std::vector<std::pair<size_t,size_t>> pairs;
        sweepPrune(bodies, pairs);

        if (pairs.empty()) continue;

        std::vector<Manifold> manifolds;
        for (auto& p : pairs) {
            if (!bodies[p.first].e->physics.collidable || !bodies[p.second].e->physics.collidable) continue;
            if (!aabbOverlap(bodies[p.first].waabb, bodies[p.second].waabb)) continue;
            Manifold m;
            if (buildManifold(bodies[p.first], bodies[p.second], m))
                manifolds.push_back(m);
        }

        if (manifolds.empty()) continue;

        for (int iter = 0; iter < ITERATIONS; iter++) {
            for (auto& m : manifolds) {
                resolve(m, stepDt);
                posCorrect(m);
            }
        }

        for (auto& b : bodies) {
            if (b.isStatic) continue;
            b.e->physics.velocity.x *= DAMPING;
            b.e->physics.velocity.y *= DAMPING;
            b.e->physics.velocity.z *= DAMPING;
        }
    }
}