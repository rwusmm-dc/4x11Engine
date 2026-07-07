// ImGui ID stack notes:
// - Every PushID() needs a matching PopID() - no early returns/continues between them.
// - TreeNodeEx() with Leaf|NoTreePushOnOpen does NOT push; do NOT call TreePop() for it.
// - TreeNodeEx() without that flag pushes only when node is open; call TreePop() only in that case.
#pragma once
#include <string>

class Scene;

namespace ProjectManagerUI {

// Show the project manager window. Returns true when a project is selected/created.
bool ShowWindow();

// State
extern bool g_Done;
extern bool g_Selected;
extern std::string g_ProjectFolder;

} // namespace ProjectManagerUI
