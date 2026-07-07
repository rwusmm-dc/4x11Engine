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
            if (GetFileAttributesA((projFolder + "\\project.4pf").c_str()) != INVALID_FILE_ATTRIBUTES ||
                GetFileAttributesA((projFolder + "\\project.4xp").c_str()) != INVALID_FILE_ATTRIBUTES) {
                projects.push_back(LoadProject(projFolder));
            }
        }
    } while (FindNextFileA(hFind, &ffd) != 0);
    FindClose(hFind);

    std::sort(projects.begin(), projects.end(),
        [](const ProjectInfo& a, const ProjectInfo& b) { return a.name < b.name; });

    return projects;
}

bool ProjectManager::CreateProject(const std::string& name)
{
    std::string baseDir = GetProjectsDirectory();
    std::string projDir = baseDir + "\\" + name;

    if (!CreateDirectoryA(projDir.c_str(), nullptr)) {
        if (GetLastError() == ERROR_ALREADY_EXISTS) return false;
        return false;
    }

    CreateDirectoryA((projDir + "\\scripts").c_str(), nullptr);
    CreateDirectoryA((projDir + "\\scenes").c_str(), nullptr);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char dateStr[64];
    snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);

    // Write project.4pf (INI-style)
    {
        std::ofstream f(projDir + "\\project.4pf");
        if (!f) return false;
        f << "[application]\n";
        f << "name = \"" << name << "\"\n";
        f << "main_scene = \"scenes/default.gaf\"\n";
        f << "icon = \"\"\n";
        f << "features = \"\"\n";
        f << "\n[autoload]\n";
        f << "\n[display]\n";
        f << "viewport_width = 1280\n";
        f << "viewport_height = 720\n";
        f << "stretch_mode = \"disabled\"\n";
        f << "\n[input]\n";
        f << "\n[physics]\n";
        f << "physics_engine = \"Bullet\"\n";
        f << "\n[rendering]\n";
        f << "driver = \"d3d11\"\n";
        f << "rendering_method = \"forward\"\n";
        f.close();
    }

    // Write legacy project.4xp for backward compatibility
    {
        std::ofstream f(projDir + "\\project.4xp");
        if (f) {
            f << "name: " << name << "\n";
            f << "scene: scenes\\default.gaf\n";
            f << "created: " << dateStr << "\n";
        }
    }

    // Create initial scene with a ServerService entity
    Entity serverSvc(1, "ServerService");
    serverSvc.SetFlag(ENTITY_SERVER_SERVICE);
    std::vector<Entity> initialScene = { serverSvc };
    std::string scenePath = projDir + "\\scenes\\default.gaf";
    if (!WriteGAF(scenePath.c_str(), initialScene)) return false;

    return true;
}

ProjectInfo ProjectManager::LoadProject(const std::string& folder)
{
    ProjectInfo info;
    info.folder = folder;

    // Try project.4pf first
    std::string pfPath = folder + "\\project.4pf";
    std::ifstream f(pfPath);
    if (f) {
        std::string line;
        std::string section;
        while (std::getline(f, line)) {
            // Trim
            size_t start = line.find_first_not_of(" \t\r");
            if (start == std::string::npos) continue;
            size_t end = line.find_last_not_of(" \t\r");
            line = line.substr(start, end - start + 1);

            if (line.empty() || line[0] == ';' || line[0] == '#') continue;

            if (line[0] == '[') {
                size_t close = line.find(']');
                if (close != std::string::npos)
                    section = line.substr(1, close - 1);
                continue;
            }

            size_t eq = line.find('=');
            if (eq == std::string::npos) continue;

            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            key.erase(0, key.find_first_not_of(" \t\r"));
            key.erase(key.find_last_not_of(" \t\r") + 1);
            val.erase(0, val.find_first_not_of(" \t\r"));
            val.erase(val.find_last_not_of(" \t\r") + 1);

            // Remove surrounding quotes
            if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                val = val.substr(1, val.size() - 2);
            if (val.size() >= 2 && val.front() == '\'' && val.back() == '\'')
                val = val.substr(1, val.size() - 2);

            if (section == "application") {
                if (key == "name") info.name = val;
                else if (key == "main_scene") info.scenePath = val;
                else if (key == "created") info.created = val;
            }
        }
        if (info.name.empty())
            info.name = folder.substr(folder.find_last_of("\\") + 1);
        if (info.scenePath.empty())
            info.scenePath = "scenes/default.gaf";
        return info;
    }

    // Fallback to project.4xp
    std::string xpPath = folder + "\\project.4xp";
    std::ifstream f2(xpPath);
    if (!f2) {
        info.name = folder.substr(folder.find_last_of("\\") + 1);
        return info;
    }

    std::string line;
    while (std::getline(f2, line)) {
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

// ── INI-style parser helper ──
static std::string TrimStr(const std::string& s)
{
    size_t start = s.find_first_not_of(" \t\r");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r");
    return s.substr(start, end - start + 1);
}

static std::string Unquote(const std::string& s)
{
    std::string t = TrimStr(s);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"')
        return t.substr(1, t.size() - 2);
    if (t.size() >= 2 && t.front() == '\'' && t.back() == '\'')
        return t.substr(1, t.size() - 2);
    return t;
}

// ── project.4pf ──

ProjectSettings ProjectManager::LoadProjectSettings(const std::string& projectFolder)
{
    ProjectSettings s;
    std::string path = projectFolder + "\\project.4pf";
    std::ifstream f(path);
    if (!f) return s;

    std::string line;
    std::string section;
    while (std::getline(f, line)) {
        line = TrimStr(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;

        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close != std::string::npos)
                section = line.substr(1, close - 1);
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = TrimStr(line.substr(0, eq));
        std::string val = Unquote(line.substr(eq + 1));

        if (section == "application") {
            if (key == "name") s.name = val;
            else if (key == "main_scene") s.mainScene = val;
        } else if (section == "display") {
            if (key == "viewport_width") s.viewportWidth = std::stoi(val);
            else if (key == "viewport_height") s.viewportHeight = std::stoi(val);
            else if (key == "stretch_mode") s.stretchMode = val;
        } else if (section == "physics") {
            if (key == "physics_engine") s.physicsEngine = val;
        } else if (section == "rendering") {
            if (key == "driver") s.driver = val;
            else if (key == "rendering_method") s.renderingMethod = val;
        } else if (section == "autoload") {
            s.autoload[key] = val;
        } else if (section == "input") {
            s.inputActions[key].push_back(val);
        }
    }
    return s;
}

bool ProjectManager::SaveProjectSettings(const std::string& projectFolder, const ProjectSettings& s)
{
    std::string path = projectFolder + "\\project.4pf";
    std::ofstream f(path);
    if (!f) return false;

    auto q = [](const std::string& v) { return "\"" + v + "\""; };

    f << "[application]\n";
    f << "name = " << q(s.name) << "\n";
    f << "main_scene = " << q(s.mainScene.empty() ? "scenes/default.gaf" : s.mainScene) << "\n";
    f << "\n[autoload]\n";
    for (auto& kv : s.autoload)
        f << kv.first << " = " << q(kv.second) << "\n";
    f << "\n[display]\n";
    f << "viewport_width = " << s.viewportWidth << "\n";
    f << "viewport_height = " << s.viewportHeight << "\n";
    f << "stretch_mode = " << q(s.stretchMode) << "\n";
    f << "\n[input]\n";
    for (auto& kv : s.inputActions)
        for (auto& ev : kv.second)
            f << kv.first << " = " << q(ev) << "\n";
    f << "\n[physics]\n";
    f << "physics_engine = " << q(s.physicsEngine) << "\n";
    f << "\n[rendering]\n";
    f << "driver = " << q(s.driver) << "\n";
    f << "rendering_method = " << q(s.renderingMethod) << "\n";
    return true;
}

// ── dir:// helpers ──

std::string ProjectManager::DirToAbsolute(const std::string& projectFolder, const std::string& dirPath)
{
    if (dirPath.find("dir://") == 0) {
        std::string rel = dirPath.substr(6); // after "dir://"
        // Normalize separators
        for (auto& c : rel) if (c == '/') c = '\\';
        // Remove leading slash
        if (!rel.empty() && (rel[0] == '\\' || rel[0] == '/'))
            rel = rel.substr(1);
        return projectFolder + "\\" + rel;
    }
    return dirPath; // not a dir:// path, return as-is
}

std::string ProjectManager::AbsoluteToDir(const std::string& projectFolder, const std::string& absolutePath)
{
    std::string normPath = absolutePath;
    for (auto& c : normPath) if (c == '/') c = '\\';

    std::string normFolder = projectFolder;
    for (auto& c : normFolder) if (c == '/') c = '\\';

    if (normPath.find(normFolder) == 0) {
        std::string rel = normPath.substr(normFolder.size());
        // Remove leading backslash
        if (!rel.empty() && (rel[0] == '\\' || rel[0] == '/'))
            rel = rel.substr(1);
        for (auto& c : rel) if (c == '\\') c = '/';
        return "dir://" + rel;
    }
    return absolutePath;
}
