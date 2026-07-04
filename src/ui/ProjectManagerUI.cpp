#include "ProjectManagerUI.h"
#include "core/Window.h"
#include "core/Project.h"
#include "imgui.h"
#include <cstdio>
#include <vector>
#include <string>
#include <windows.h>

namespace ProjectManagerUI {

bool g_Done = false;
bool g_Selected = false;
std::string g_ProjectFolder;

bool ShowWindow()
{
    if (g_Done) return true;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 winSize = io.DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(winSize);

    ImGui::Begin("4xEngine Project Manager", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings);

    ImGui::SetWindowFontScale(1.5f);
    ImGui::TextColored(ImVec4(0.3f, 0.6f, 1.0f, 1), "Project Manager");
    ImGui::SetWindowFontScale(1.0f);
    ImGui::Separator();

    float panelW = ImGui::GetContentRegionAvail().x;
    float panelH = ImGui::GetContentRegionAvail().y;
    float leftW = panelW * 0.45f;

    ImGui::BeginChild("ProjectList", ImVec2(leftW, panelH * 0.8f), true);
    ImGui::Text("Existing Projects:");
    ImGui::Separator();

    static std::vector<ProjectInfo> cachedProjects;
    static bool needRefresh = true;
    if (needRefresh) {
        cachedProjects = ProjectManager::ListProjects();
        needRefresh = false;
    }
    if (ImGui::Button("Refresh")) {
        needRefresh = true;
    }

    static int selectedProject = -1;
    for (size_t i = 0; i < cachedProjects.size(); i++) {
        char label[256];
        snprintf(label, sizeof(label), "%s  (%s)", cachedProjects[i].name.c_str(),
                 cachedProjects[i].created.c_str());
        if (ImGui::Selectable(label, selectedProject == (int)i)) {
            selectedProject = (int)i;
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("ProjectActions", ImVec2(leftW, panelH * 0.8f), true);
    ImGui::Text("Actions:");
    ImGui::Separator();

    if (selectedProject >= 0 && selectedProject < (int)cachedProjects.size()) {
        if (ImGui::Button("Open Project", ImVec2(-1, 36))) {
            g_ProjectFolder = cachedProjects[selectedProject].folder;
            g_Selected = true;
            g_Done = true;
            ImGui::EndChild();
            ImGui::End();
            return true;
        }
        ImGui::Dummy(ImVec2(0, 8));
        if (ImGui::Button("Delete Project", ImVec2(-1, 36))) {
            char msg[512];
            snprintf(msg, sizeof(msg), "Delete project '%s' permanently?",
                     cachedProjects[selectedProject].name.c_str());
            if (MessageBoxA(Window::Handle(), msg, "Confirm Delete", MB_YESNO | MB_ICONWARNING) == IDYES) {
                ProjectManager::DeleteProject(cachedProjects[selectedProject].folder);
                needRefresh = true;
                selectedProject = -1;
            }
        }
    } else {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "Select a project from the list");
    }

    ImGui::Dummy(ImVec2(0, 16));
    ImGui::Separator();
    ImGui::Text("Create New Project:");
    ImGui::Dummy(ImVec2(0, 4));

    static char newName[128] = {};
    ImGui::InputText("Project Name", newName, sizeof(newName));
    if (ImGui::Button("Create && Open", ImVec2(-1, 36)) && strlen(newName) > 0) {
        if (ProjectManager::CreateProject(newName)) {
            g_ProjectFolder = ProjectManager::GetProjectsDirectory() + "\\" + newName;
            g_Selected = true;
            g_Done = true;
            newName[0] = '\0';
            ImGui::EndChild();
            ImGui::End();
            return true;
        } else {
            MessageBoxA(Window::Handle(), "Failed to create project (may already exist)", "Error", MB_ICONERROR);
        }
    }

    ImGui::EndChild();

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Projects are stored in: %s",
        ProjectManager::GetProjectsDirectory().c_str());
    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "4xLang v0.1");

    ImGui::End();

    if (g_Done) {
        HKEY hKey;
        if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\4xEngine", 0, nullptr, 0,
            KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
            RegSetValueExA(hKey, "RecentProject", 0, REG_SZ,
                (const BYTE*)g_ProjectFolder.c_str(),
                (DWORD)g_ProjectFolder.size() + 1);
            RegCloseKey(hKey);
        }
    }

    return g_Done;
}

} // namespace ProjectManagerUI
