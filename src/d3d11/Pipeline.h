#pragma once
#include <DirectXMath.h>
#include "core/ComPtr.h"
#include <cstdint>

namespace d3d11 {

// Two simultaneous lights: a directional sun light (from the skybox/time-of-day
// system or a Directional light entity) and one point light (accumulated from
// Point light entities in the scene).
struct LightData {
    float sunDir[4];      // xyz = direction TOWARDS the sun (normalized), w unused
    float sunColor[4];    // rgb = color, a = intensity/enabled multiplier
    float pointPos[4];    // xyz = world position, w unused
    float pointColor[4];  // rgb = color, a = intensity
    float pointParam[4];  // x = range, yzw unused
    float ambient[4];     // rgb = ambient color, a unused
    float cameraPos[4];   // xyz = camera world position, w unused
    float flags[4];       // x = sunEnabled (0/1), y = pointEnabled (0/1), zw unused
};

namespace Pipeline {

bool Init(int width, int height);
void Shutdown();
void Bind();
void SetViewProj(DirectX::XMMATRIX view, DirectX::XMMATRIX proj);
void SetLightData(const LightData& data);

void DrawEntity(uint64_t entityId, DirectX::XMMATRIX world,
                const float* verts, int vertCount,
                const uint32_t* indices, int indexCount,
                bool meshDirty);
void DrawEntityInstanced(uint64_t entityId,
                const DirectX::XMMATRIX* worlds, int instanceCount,
                const float* verts, int vertCount,
                const uint32_t* indices, int indexCount,
                bool meshDirty);
void RemoveEntityMesh(uint64_t entityId);
int DrawCallCount();

} // namespace Pipeline
} // namespace d3d11