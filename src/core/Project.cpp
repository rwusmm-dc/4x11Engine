#include "Project.h"
#include "ecs/ECS.h"
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>

static std::string GetDocumentsFolder()
{
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, 0, path))) {
        char buf[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, path, -1, buf, MAX_PATH, nullptr, nullptr);
        return std::string(buf);
    }
    return std::string(getenv("USERPROFILE")) + "\\Documents";
}

std::string ProjectManager::GetProjectsDirectory()
{
    return GetDocumentsFolder() + "\\4xEngine\\Projects";
}

std::vector<ProjectInfo> ProjectManager::ListProjects()
{
    std::vector<ProjectInfo> projects;
    std::string dir = GetProjectsDirectory();

    // Ensure the directory exists
    CreateDirectoryA((GetDocumentsFolder() + "\\4xEngine").c_str(), nullptr);
    CreateDirectoryA(dir.c_str(), nullptr);

    WIN32_FIND_DATAA ffd;
    std::string search = dir + "\\*";
    HANDLE hFind = FindFirstFileA(search.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return projects;

    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0)
                continue;
            std::string projFolder = dir + "\\" + ffd.cFileName;
            std::string projFile = projFolder + "\\project.4xp";
            // Check if project.4xp exists
            if (GetFileAttributesA(projFile.c_str()) != INVALID_FILE_ATTRIBUTES) {
                projects.push_back(LoadProject(projFolder));
            }
        }
    } while (FindNextFileA(hFind, &ffd) != 0);
    FindClose(hFind);

    // Sort by name
    std::sort(projects.begin(), projects.end(),
        [](const ProjectInfo& a, const ProjectInfo& b) { return a.name < b.name; });

    return projects;
}

bool ProjectManager::CreateProject(const std::string& name)
{
    std::string baseDir = GetProjectsDirectory();
    std::string projDir = baseDir + "\\" + name;

    // Create directory structure
    if (!CreateDirectoryA(projDir.c_str(), nullptr)) {
        if (GetLastError() == ERROR_ALREADY_EXISTS) return false;
        return false;
    }

    // Create subdirectories
    CreateDirectoryA((projDir + "\\scripts").c_str(), nullptr);
    CreateDirectoryA((projDir + "\\scenes").c_str(), nullptr);

    // Get current date
    SYSTEMTIME st;
    GetLocalTime(&st);
    char dateStr[64];
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);

    // Create project.4xp
    ProjectInfo info;
    info.name = name;
    info.folder = projDir;
    info.scenePath = "scenes\\default.gaf";
    info.created = dateStr;
    if (!SaveProject(info)) return false;

    // Create initial scene with a ServerService entity
    Entity serverSvc(1, "ServerService");
    serverSvc.SetFlag(ENTITY_SERVER_SERVICE);
    // Send to back: set a very large parentId so it sorts first in any list
    std::vector<Entity> initialScene = { serverSvc };
    std::string scenePath = projDir + "\\" + info.scenePath;
    if (!WriteGAF(scenePath.c_str(), initialScene)) return false;

    return true;
}

ProjectInfo ProjectManager::LoadProject(const std::string& folder)
{
    ProjectInfo info;
    info.folder = folder;

    std::string path = folder + "\\project.4xp";
    std::ifstream f(path);
    if (!f) {
        info.name = folder.substr(folder.find_last_of("\\") + 1);
        return info;
    }

    std::string line;
    while (std::getline(f, line)) {
        if (line.find("name:") == 0)
            info.name = line.substr(5);
        else if (line.find("scene:") == 0)
            info.scenePath = line.substr(6);
        else if (line.find("created:") == 0)
            info.created = line.substr(8);
    }
    return info;
}

bool ProjectManager::SaveProject(const ProjectInfo& project)
{
    std::string path = project.folder + "\\project.4xp";
    std::ofstream f(path);
    if (!f) return false;
    f << "name: " << project.name << "\n";
    f << "scene: " << project.scenePath << "\n";
    f << "created: " << project.created << "\n";
    return true;
}

bool ProjectManager::DeleteProject(const std::string& folder)
{
    // Simple recursive delete using shell
    SHFILEOPSTRUCTA fos = {};
    fos.wFunc = FO_DELETE;
    std::string dir = folder + '\0';
    fos.pFrom = dir.c_str();
    fos.fFlags = FOF_NO_UI | FOF_SILENT;
    return SHFileOperationA(&fos) == 0;
}

std::string ProjectManager::GetScriptsFolder(const std::string& projectFolder)
{
    return projectFolder + "\\scripts";
}

std::string ProjectManager::GetScenesFolder(const std::string& projectFolder)
{
    return projectFolder + "\\scenes";
}
