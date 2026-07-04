#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include "core/Window.h"

class Scene;
struct Entity;

namespace Overlay {

bool Init(HWND hwnd, void* device, void* context, GfxBackend backend);
void NewFrame();
void BeginFrame();
bool DrawMenuBar(Scene& scene); // returns true if export triggered
void ShowPerformanceWindow(GfxBackend backend, int culledTotal = 0, int culledVisible = 0);
void ShowHierarchyWindow(Scene& scene, uint64_t& selectedEntity, bool& deselect);
bool ShowPropertiesWindow(Entity* entity, Scene& scene);
void ShowEditorWindow(Scene& scene, Entity* entity);
void ShowServerServiceWindow(Scene& scene);
void ShowGizmo(const float* view, const float* projection, Entity* entity);
void DrawLightIcons(Scene& scene, const float* view, const float* projection, int viewportW, int viewportH);
void DrawCameraIcons(Scene& scene, const float* view, const float* projection, int viewportW, int viewportH);
bool IsGizmoOver();
void SaveSceneToProject(Scene& scene);
void EndFrame(void* context, GfxBackend backend);
void Shutdown();

// Script editing state
extern bool g_ShowEditor;
extern std::string g_EditorPath;
extern uint64_t g_EditingEntity;

// Debug console
void ShowDebugConsole(bool* pOpen);

} // namespace Overlay
