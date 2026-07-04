#pragma once
#include <string>
#include <vector>

// 4xLang v0.1 — Project Manager
// Each project lives in Documents/4xEngine/Projects/<name>/
struct ProjectInfo {
    std::string name;
    std::string folder;       // full path to project folder
    std::string scenePath;    // relative path from project folder
    std::string created;      // creation date string
};

namespace ProjectManager {

// Get the base projects directory (Documents/4xEngine/Projects/)
std::string GetProjectsDirectory();

// List all existing projects
std::vector<ProjectInfo> ListProjects();

// Create a new project
bool CreateProject(const std::string& name);

// Load a project's metadata from its project.4xp file
ProjectInfo LoadProject(const std::string& folder);

// Save a project's metadata to project.4xp
bool SaveProject(const ProjectInfo& project);

// Delete a project folder
bool DeleteProject(const std::string& folder);

// Get the scripts folder for a project
std::string GetScriptsFolder(const std::string& projectFolder);

// Get the scenes folder for a project
std::string GetScenesFolder(const std::string& projectFolder);

} // namespace ProjectManager
