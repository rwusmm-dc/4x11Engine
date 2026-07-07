// ImGui ID stack notes:
// - Every PushID() needs a matching PopID() - no early returns/continues between them.
// - TreeNodeEx() with Leaf|NoTreePushOnOpen does NOT push; do NOT call TreePop() for it.
// - TreeNodeEx() without that flag pushes only when node is open; call TreePop() only in that case.
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
    void ShowGizmo(const float* view, const float* projection, Entity* entity, Scene& scene);
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

void ShowDebugConsole(bool* pOpen);

// ── Docking ──
void BeginDockspace();
void EndDockspace();

// ── Directory Manager (docked at bottom) ──
void ShowDirectoryManagerWindow();

} // namespace Overlay
