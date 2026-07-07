#pragma once
#include <DirectXMath.h>

namespace d3d10 {
namespace Skybox {

struct TimeState {
    float timeOfDay;   // 0-24 hours
    float speed;       // hours per second
    float sunDir[3];   // normalized direction to sun
    float sunColor[3]; // rgb * intensity
    float skyColor[3]; // ambient sky tint
};

struct SunLight {
    float direction[3];
    float color[3];
    float intensity;
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
} // namespace d3d10
