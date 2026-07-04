#pragma once
#include <DirectXMath.h>

namespace d3d11 {
namespace Skybox {

struct TimeState {
    float timeOfDay;   // 0-24 hours
    float speed;       // hours per second
    float sunDir[3];   // normalized direction to sun
    float sunColor[3]; // rgb * intensity
    float skyColor[3]; // ambient sky tint
};

// Sun light data ready to feed straight into Pipeline::LightData's
// sunDir/sunColor fields.
struct SunLight {
    float direction[3]; // normalized direction TOWARDS the sun
    float color[3];     // rgb
    float intensity;    // scalar multiplier (already baked into color by the sky sim,
                         // kept at 1.0 here so callers can just copy color as-is)
};

bool Init();
void Shutdown();
void Update(float deltaTime);
void Draw(const float* viewMatrix, const float* projMatrix, float aspectRatio, float totalTime);

TimeState GetTimeState();
SunLight GetSunLight();
void SetTimeOfDay(float hours);
void SetTimeSpeed(float hoursPerSec);
void SetSkyColor(float r, float g, float b);
void ClearSkyColorOverride();
void SetEnabled(bool enabled);
bool IsEnabled();

} // namespace Skybox
} // namespace d3d11