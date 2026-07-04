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
