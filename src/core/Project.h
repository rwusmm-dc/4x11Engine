#pragma once
#include <string>
#include <vector>
#include <unordered_map>

// 4xLang v0.1 — Project Manager
// Each project lives in Documents/4xEngine/Projects/<name>/
struct ProjectInfo {
    std::string name;
    std::string folder;       // full path to project folder
    std::string scenePath;    // relative path from project folder
    std::string created;      // creation date string
};

// Godot-style INI-based project settings (project.4pf)
struct ProjectSettings {
    // [application]
    std::string name;
    std::string mainScene;
    std::string icon;
    std::string features;

    // [display]
    int viewportWidth = 1280;
    int viewportHeight = 720;
    std::string stretchMode = "disabled";

    // [physics]
    std::string physicsEngine = "Bullet";

    // [rendering]
    std::string driver = "d3d11";
    std::string renderingMethod = "forward";

    // [autoload] — key = path
    std::unordered_map<std::string, std::string> autoload;

    // [input] — action name -> list of event strings
    std::unordered_map<std::string, std::vector<std::string>> inputActions;
};

namespace ProjectManager {

// Get the base projects directory (Documents/4xEngine/Projects/)
std::string GetProjectsDirectory();

// List all existing projects (checks both project.4xp and project.4pf)
std::vector<ProjectInfo> ListProjects();

// Create a new project (creates project.4pf)
bool CreateProject(const std::string& name);

// Load a project's metadata from project.4xp or project.4pf
ProjectInfo LoadProject(const std::string& folder);

// Save a project's metadata to project.4xp
bool SaveProject(const ProjectInfo& project);

// Delete a project folder
bool DeleteProject(const std::string& folder);

// Get the scripts folder for a project
std::string GetScriptsFolder(const std::string& projectFolder);

// Get the scenes folder for a project
std::string GetScenesFolder(const std::string& projectFolder);

// ── project.4pf (INI-style settings) ──

// Load project.4pf settings (returns defaults if file missing)
ProjectSettings LoadProjectSettings(const std::string& projectFolder);

// Save project.4pf settings
bool SaveProjectSettings(const std::string& projectFolder, const ProjectSettings& settings);

// ── dir:// virtual filesystem helpers ──

// Resolve "dir://..." path to absolute path using project folder
std::string DirToAbsolute(const std::string& projectFolder, const std::string& dirPath);

// Convert absolute path to "dir://..." relative path
std::string AbsoluteToDir(const std::string& projectFolder, const std::string& absolutePath);

} // namespace ProjectManager
