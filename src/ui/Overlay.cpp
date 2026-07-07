// ImGui ID stack notes:
// - Every PushID() needs a matching PopID() - no early returns/continues between them.
// - TreeNodeEx() with Leaf|NoTreePushOnOpen does NOT push; do NOT call TreePop() for it.
// - TreeNodeEx() without that flag pushes only when node is open; call TreePop() only in that case.
#include "Overlay.h"
#include "ProjectManagerUI.h"
#include "core/Window.h"
#include "core/ObjLoader.h"
#include "core/Project.h"
#include "ecs/ECS.h"
#include "script/ScriptEngine.h"
#include "CodeEditor.h"
#include "phy/4xPhys.h"

#include "d3d10/Device.h"
#include "d3d10/Pipeline.h"
#include "d3d10/skybox.h"
#include "d3d11/Device.h"
#include "d3d11/Pipeline.h"
#include "d3d11/skybox.h"
#include "imgui.h"
#include "../gizmo/Gizmo.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx10.h"
#include "backends/imgui_impl_dx11.h"
#include <DirectXMath.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <shellapi.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>

// ── imguitheme.ini loader ──
static const char* g_ColorNames[] = {
    "Text", "TextDisabled", "WindowBg", "ChildBg", "PopupBg",
    "Border", "BorderShadow", "FrameBg", "FrameBgHovered", "FrameBgActive",
    "TitleBg", "TitleBgActive", "TitleBgCollapsed", "MenuBarBg", "ScrollbarBg",
    "ScrollbarGrab", "ScrollbarGrabHovered", "ScrollbarGrabActive",
    "CheckMark", "CheckboxSelectedBg", "SliderGrab", "SliderGrabActive",
    "Button", "ButtonHovered", "ButtonActive",
    "Header", "HeaderHovered", "HeaderActive",
    "Separator", "SeparatorHovered", "SeparatorActive",
    "ResizeGrip", "ResizeGripHovered", "ResizeGripActive",
    "InputTextCursor",
    "TabHovered", "Tab", "TabSelected", "TabSelectedOverline",
    "TabDimmed", "TabDimmedSelected", "TabDimmedSelectedOverline",
    "PlotLines", "PlotLinesHovered", "PlotHistogram", "PlotHistogramHovered",
    "TableHeaderBg", "TableBorderStrong", "TableBorderLight",
    "TableRowBg", "TableRowBgAlt",
    "TextLink", "TextSelectedBg", "TreeLines",
    "DragDropTarget", "DragDropTargetBg", "UnsavedMarker",
    "NavCursor", "NavWindowingHighlight", "NavWindowingDimBg", "ModalWindowDimBg",
};

static void ApplyThemeFromINI()
{
    std::ifstream f("imguitheme.ini");
    if (!f.is_open()) {
        ImGui::StyleColorsDark();
        return;
    }

    ImGui::StyleColorsDark();
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '[' || line[0] == ';' || line[0] == '#')
            continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string name = line.substr(0, eq);
        std::string val  = line.substr(eq + 1);
        name.erase(0, name.find_first_not_of(" \t\r"));
        name.erase(name.find_last_not_of(" \t\r") + 1);
        val.erase(0, val.find_first_not_of(" \t\r"));
        val.erase(val.find_last_not_of(" \t\r") + 1);

        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            if (name == g_ColorNames[i]) {
                float r, g, b, a;
                if (sscanf(val.c_str(), "%f %f %f %f", &r, &g, &b, &a) == 4)
                    ImGui::GetStyle().Colors[i] = ImVec4(r, g, b, a);
                break;
            }
        }
    }
}

static void SaveThemeToINI()
{
    std::ofstream f("imguitheme.ini");
    if (!f.is_open()) return;
    f << "[Colors]\n";
    for (int i = 0; i < ImGuiCol_COUNT; i++) {
        ImVec4 c = ImGui::GetStyle().Colors[i];
        f << g_ColorNames[i] << " = " << c.x << " " << c.y << " " << c.z << " " << c.w << "\n";
    }
    f.close();
}

namespace {

GfxBackend g_Backend = GfxBackend::None;

double  g_Freq = 0.0;
int64_t g_LastTick = 0;
float   g_FPS = 0.0f;
float   g_FrameTime = 0.0f;

HANDLE  g_Process = nullptr;
int64_t g_PrevProcessTime = 0;
int64_t g_PrevTimestamp = 0;

MEMORYSTATUSEX g_MemState = {};

wchar_t g_GPUName[128] = {};
uint64_t g_GPUMemTotal = 0;

struct D3D10QueryData {
    ComPtr<ID3D10Query> disjoint;
    ComPtr<ID3D10Query> start;
    ComPtr<ID3D10Query> end;
} g_Q10;

struct D3D11QueryData {
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> start;
    ComPtr<ID3D11Query> end;
} g_Q11;

float g_LastGPUFrameTime = 0.0f;

static Gizmo g_Gizmo;

static int64_t FileTimeToInt64(const FILETIME& ft)
{
    return (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

static void InitStats()
{
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_Freq = (double)freq.QuadPart;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&g_LastTick));
    g_Process = GetCurrentProcess();
    FILETIME ftCreation, ftExit, ftKernel, ftUser;
    GetProcessTimes(g_Process, &ftCreation, &ftExit, &ftKernel, &ftUser);
    g_PrevProcessTime = FileTimeToInt64(ftKernel) + FileTimeToInt64(ftUser);
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&g_PrevTimestamp));
    g_MemState.dwLength = sizeof(g_MemState);
}

static void UpdateFPS()
{
    int64_t now;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&now));
    g_FrameTime = (float)((double)(now - g_LastTick) / g_Freq);
    g_LastTick = now;
    g_FPS = (g_FrameTime > 0.0f) ? (1.0f / g_FrameTime) : 0.0f;
}

static float GetCPUUsagePercent()
{
    FILETIME ftCreation, ftExit, ftKernel, ftUser;
    GetProcessTimes(g_Process, &ftCreation, &ftExit, &ftKernel, &ftUser);
    int64_t processTime = FileTimeToInt64(ftKernel) + FileTimeToInt64(ftUser);
    int64_t now;
    QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&now));
    double dtWall = (double)(now - g_PrevTimestamp) / g_Freq;
    int64_t dtProcess = processTime - g_PrevProcessTime;
    g_PrevProcessTime = processTime;
    g_PrevTimestamp = now;
    if (dtWall <= 0.0) return 0.0f;
    double percent = (double)dtProcess / 10000000.0 / dtWall * 100.0;
    static float smooth = 0.0f;
    smooth = smooth * 0.85f + (float)percent * 0.15f;
    return smooth;
}

static void UpdateRAM()
{
    GlobalMemoryStatusEx(&g_MemState);
}

static void InitGPUInfo(void* device, GfxBackend)
{
    IUnknown* unk = static_cast<IUnknown*>(device);
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    if (SUCCEEDED(unk->QueryInterface(__uuidof(IDXGIDevice), (void**)dxgiDevice.addr()))) {
        if (SUCCEEDED(dxgiDevice->GetAdapter(adapter.addr()))) {
            DXGI_ADAPTER_DESC desc;
            if (SUCCEEDED(adapter->GetDesc(&desc))) {
                wcscpy_s(g_GPUName, desc.Description);
                g_GPUMemTotal = desc.DedicatedVideoMemory;
            }
        }
    }
}

static bool ImportOBJToScene(Scene& scene)
{
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = Window::Handle();
    ofn.lpstrFilter     = "OBJ Files\0*.obj\0All Files\0*.*\0";
    ofn.lpstrFile       = path;
    ofn.nMaxFile        = MAX_PATH;
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    path[0] = '\0';
    if (!GetOpenFileNameA(&ofn)) return false;

    ObjMesh mesh;
    if (!LoadObj(path, mesh)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Failed to load OBJ:\n%s", path);
        MessageBoxA(Window::Handle(), msg, "Error", MB_ICONERROR);
        return false;
    }

    const char* nameStart = path;
    for (const char* p = path; *p; p++)
        if (*p == '\\' || *p == '/') nameStart = p + 1;

    std::string entityName(nameStart);
    size_t dot = entityName.rfind('.');
    if (dot != std::string::npos) entityName.resize(dot);
    if (entityName.empty()) entityName = "Imported";

    uint64_t id = scene.CreateEntity(entityName);
    Entity* e = scene.FindEntity(id);
    e->vertices = std::move(mesh.vertices);
    e->indices  = std::move(mesh.indices);
    e->meshDirty = true;
    PhysWorld4X::BuildCollisionMesh(e->vertices, e->collisionVertices, path);
    return true;
}

} // anonymous namespace

// ── Global editor state (4xLang v0.1) ──
extern bool g_PlayMode;
extern bool g_PlayRequest;

namespace Overlay {
bool g_ShowEditor = false;
std::string g_EditorPath;
uint64_t g_EditingEntity = 0;
}

static TextEditor g_Editor;
static bool g_EditorTextChanged = false;

static bool ImportSdkMeshToScene(Scene& scene)
{
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = Window::Handle();
    ofn.lpstrFilter = "SDKMESH Files\0*.sdkmesh\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    path[0] = '\0';
    if (!GetOpenFileNameA(&ofn))
        return false;

    ObjMesh mesh;
    if (!LoadSdkMeshAsObj(path, mesh)) {
        char msg[512];
        snprintf(msg, sizeof(msg), "Failed to load SDKMESH:\n%s", path);
        MessageBoxA(Window::Handle(), msg, "Error", MB_ICONERROR);
        return false;
    }

    const char* nameStart = path;
    for (const char* p = path; *p; p++) {
        if (*p == '\\' || *p == '/')
            nameStart = p + 1;
    }

    std::string entityName(nameStart);
    size_t dot = entityName.rfind('.');
    if (dot != std::string::npos)
        entityName.resize(dot);
    if (entityName.empty())
        entityName = "ImportedSDK";

    uint64_t id = scene.CreateEntity(entityName);
    Entity* e = scene.FindEntity(id);
    e->vertices = std::move(mesh.vertices);
    e->indices = std::move(mesh.indices);
    e->meshDirty = true;
    PhysWorld4X::BuildCollisionMesh(e->vertices, e->collisionVertices, path);
    return true;
}

// ── Helper: open file dialog for .scr / .4xs scripts ──
static bool OpenScriptFileDialog(char* pathOut, int pathSize)
{
    OPENFILENAMEA ofn = {};
    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = Window::Handle();
    ofn.lpstrFilter     = "4xLang Scripts\0*.scr;*.4xs\0All Files\0*.*\0";
    ofn.lpstrFile       = pathOut;
    ofn.nMaxFile        = pathSize;
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    pathOut[0] = '\0';
    return GetOpenFileNameA(&ofn) != 0;
}

// ── Helper: extract filename from path ──
static std::string GetFileName(const std::string& path)
{
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

// ── Helper: read file into string ──
static std::string ReadFileToString(const std::string& path)
{
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// ── Helper: write string to file ──
static bool WriteStringToFile(const std::string& path, const std::string& content)
{
    std::ofstream f(path);
    if (!f) return false;
    f << content;
    return true;
}

static bool EntityNameLess(const Entity* a, const Entity* b) {
    return a->name < b->name;
}

// ── Helper: recursively render entity hierarchy (with auto-sort) ──
static void RenderEntityTree(Scene& scene, Entity& entity, uint64_t& selectedEntity)
{
    ImGui::PushID(&entity);

    // Collect children (sorted)
    std::vector<Entity*> children;
    for (auto& e : scene.All())
        if (e.parentId == entity.id && e.id != entity.id)
            children.push_back(&e);
    std::sort(children.begin(), children.end(), EntityNameLess);
    bool hasChildren = !children.empty();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (entity.id == selectedEntity)
        flags |= ImGuiTreeNodeFlags_Selected;

    std::string label;
    if (entity.HasFlag(ENTITY_WORLD_ENV)) {
        label = "[Wld] " + entity.name;
    } else if (entity.HasFlag(ENTITY_SERVER_SERVICE)) {
        label = "[Srv] " + entity.name;
    } else if (entity.HasFlag(ENTITY_SCRIPT)) {
        label = "[Scr] " + entity.name;
    } else if (entity.HasFlag(ENTITY_MODEL)) {
        label = "[Mdl] " + entity.name;
    } else if (entity.HasFlag(ENTITY_LIGHT)) {
        label = (entity.light.type == LightType::Directional) ? "[Dir] " : "[Pnt] ";
        label += entity.name;
    } else if (entity.HasFlag(ENTITY_CAMERA)) {
        label = "[Cam] " + entity.name;
    } else {
        label = entity.name;
    }

    bool open = ImGui::TreeNodeEx(label.c_str(), flags);
    if (ImGui::IsItemClicked() && !ImGui::IsMouseDragging(0)) {
        selectedEntity = entity.id;
    }

    // Dragging support: drag source
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("ENTITY_NODE", &entity.id, sizeof(uint64_t));
        ImGui::Text("%s", label.c_str());
        ImGui::EndDragDropSource();
    }

    // Drop target: reparent (Models, regular entities, root space)
    bool canDrop = !entity.HasFlag(ENTITY_LIGHT) && !entity.HasFlag(ENTITY_CAMERA)
                   && !entity.HasFlag(ENTITY_WORLD_ENV);
    if (canDrop && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE")) {
            uint64_t dragId = *(const uint64_t*)payload->Data;
            if (dragId != entity.id) {
                scene.ReparentEntity(dragId, entity.id);
            }
        }
        ImGui::EndDragDropTarget();
    }

    if (open && hasChildren) {
        for (auto* child : children)
            RenderEntityTree(scene, *child, selectedEntity);
        ImGui::TreePop();
    }

    ImGui::PopID();
}

namespace Overlay {

bool Init(HWND hwnd, void* device, void* context, GfxBackend backend)
{
    g_Backend = backend;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplyThemeFromINI();

    if (!ImGui_ImplWin32_Init(hwnd)) return false;

    if (backend == GfxBackend::D3D10) {
        if (!ImGui_ImplDX10_Init(static_cast<ID3D10Device*>(device))) return false;
    } else if (backend == GfxBackend::D3D11) {
        if (!ImGui_ImplDX11_Init(static_cast<ID3D11Device*>(device),
                                 static_cast<ID3D11DeviceContext*>(context))) return false;
    }

    InitStats();
    InitGPUInfo(device, backend);

    if (backend == GfxBackend::D3D10) {
        ID3D10Device* dev = static_cast<ID3D10Device*>(device);
        D3D10_QUERY_DESC qd = {};
        qd.Query = D3D10_QUERY_TIMESTAMP_DISJOINT;
        if (SUCCEEDED(dev->CreateQuery(&qd, g_Q10.disjoint.addr()))) {
            qd.Query = D3D10_QUERY_TIMESTAMP;
            if (FAILED(dev->CreateQuery(&qd, g_Q10.start.addr())) ||
                FAILED(dev->CreateQuery(&qd, g_Q10.end.addr()))) {
                g_Q10.disjoint.release();
            }
        }
    } else if (backend == GfxBackend::D3D11) {
        ID3D11Device* dev = static_cast<ID3D11Device*>(device);
        D3D11_QUERY_DESC qd = {};
        qd.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
        if (SUCCEEDED(dev->CreateQuery(&qd, g_Q11.disjoint.addr()))) {
            qd.Query = D3D11_QUERY_TIMESTAMP;
            if (FAILED(dev->CreateQuery(&qd, g_Q11.start.addr())) ||
                FAILED(dev->CreateQuery(&qd, g_Q11.end.addr()))) {
                g_Q11.disjoint.release();
            }
        }
    }

    return true;
}

void NewFrame()
{
    UpdateFPS();
    UpdateRAM();
}

void BeginFrame()
{
    if (g_Backend == GfxBackend::D3D10 && g_Q10.disjoint && g_Q10.start && g_Q10.end) {
        D3D10_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
        UINT64 startTime, endTime;
        if (g_Q10.disjoint->GetData(&disjointData, sizeof(disjointData), 0) == S_OK &&
            g_Q10.start->GetData(&startTime, sizeof(startTime), 0) == S_OK &&
            g_Q10.end->GetData(&endTime, sizeof(endTime), 0) == S_OK &&
            !disjointData.Disjoint) {
            g_LastGPUFrameTime = (float)((double)(endTime - startTime) / (double)disjointData.Frequency);
        }
    } else if (g_Backend == GfxBackend::D3D11 && g_Q11.disjoint && g_Q11.start && g_Q11.end) {
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
        UINT64 startTime, endTime;
        ID3D11DeviceContext* ctx = d3d11::Device::GetCtx();
        if (ctx->GetData(g_Q11.disjoint.get(), &disjointData, sizeof(disjointData), 0) == S_OK &&
            ctx->GetData(g_Q11.start.get(), &startTime, sizeof(startTime), 0) == S_OK &&
            ctx->GetData(g_Q11.end.get(), &endTime, sizeof(endTime), 0) == S_OK &&
            !disjointData.Disjoint) {
            g_LastGPUFrameTime = (float)((double)(endTime - startTime) / (double)disjointData.Frequency);
        }
    }

    if (g_Backend == GfxBackend::D3D10) {
        if (g_Q10.disjoint) g_Q10.disjoint->Begin();
        if (g_Q10.start) g_Q10.start->End();
    } else if (g_Backend == GfxBackend::D3D11) {
        ID3D11DeviceContext* ctx = d3d11::Device::GetCtx();
        if (g_Q11.disjoint) ctx->Begin(g_Q11.disjoint.get());
        if (g_Q11.start) ctx->End(g_Q11.start.get());
    }

    if (g_Backend == GfxBackend::D3D10) ImGui_ImplDX10_NewFrame();
    else                                 ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

}

void ShowPerformanceWindow(GfxBackend backend, int culledTotal, int culledVisible)
{
    ImGui::SetNextWindowPos(ImVec2(3, 33), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(351, 246), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.65f);
    ImGui::Begin("Performance", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::Text("DirectX Version:   %s", (backend == GfxBackend::D3D10) ? "Direct3D 10" : "Direct3D 11");
    ImGui::Separator();
    ImGui::Text("FPS:               %.1f", g_FPS);
    ImGui::Text("Frame Time:        %.2f ms", g_FrameTime * 1000.0f);
    int dc = (backend == GfxBackend::D3D10) ? d3d10::Pipeline::DrawCallCount()
                                            : d3d11::Pipeline::DrawCallCount();
    ImGui::Text("Draw Calls:        %d", dc);
    ImGui::Separator();
    ImGui::Text("Culling Visible:   %d / %d", culledVisible, culledTotal);
    ImGui::Separator();
    float cpu = GetCPUUsagePercent();
    ImGui::Text("CPU Usage:         %.1f %%", cpu);
    float ramPercent = (float)g_MemState.dwMemoryLoad;
    float ramUsedMB  = (float)(g_MemState.ullTotalPhys - g_MemState.ullAvailPhys) / (1024.0f * 1024.0f);
    float ramTotalMB = (float)g_MemState.ullTotalPhys / (1024.0f * 1024.0f);
    ImGui::Text("RAM Usage:         %.1f / %.1f MB (%.0f %%)", ramUsedMB, ramTotalMB, ramPercent);
    ImGui::Separator();
    char gpuNameA[256];
    WideCharToMultiByte(CP_UTF8, 0, g_GPUName, -1, gpuNameA, sizeof(gpuNameA), nullptr, nullptr);
    float gpuMemMB = (float)g_GPUMemTotal / (1024.0f * 1024.0f);
    ImGui::Text("GPU:               %s", gpuNameA);
    ImGui::Text("GPU Memory:        %.0f MB", gpuMemMB);
    if (g_LastGPUFrameTime > 0.0f && g_FrameTime > 0.0f) {
        float gpuPercent = (g_LastGPUFrameTime / g_FrameTime) * 100.0f;
        if (gpuPercent > 100.0f) gpuPercent = 100.0f;
        ImGui::Text("GPU Usage:         %.1f %%", gpuPercent);
    } else {
        ImGui::Text("GPU Usage:         0.0 %%");
    }
    ImGui::Separator();
    ImGui::Text("VB + IB:           %.2f KB", 0.0f);
    ImGui::Text("Constant Buffer:   %zu bytes", sizeof(float) * 16);

    ImGui::End();
}

void ShowHierarchyWindow(Scene& scene, uint64_t& selectedEntity, bool& deselect)
{
    ImGui::SetNextWindowPos(ImVec2(1102, 22), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(255, 244), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Hierarchy", nullptr);

    // Drag-drop target on empty space of hierarchy to reparent to root
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE")) {
            uint64_t dragId = *(const uint64_t*)payload->Data;
            scene.ReparentEntity(dragId, 0);
        }
        ImGui::EndDragDropTarget();
    }

    // Entity tree: auto-sort by name, render root entities (parentId == 0)
    std::vector<Entity*> sorted;
    for (auto& e : scene.All())
        if (e.parentId == 0)
            sorted.push_back(&e);
    std::sort(sorted.begin(), sorted.end(), EntityNameLess);
    for (auto* e : sorted)
        RenderEntityTree(scene, *e, selectedEntity);


    // Deselect when clicking empty space in the hierarchy window
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered()) {
        selectedEntity = 0;
    }

    // Right-click context menu on empty space
    if (ImGui::BeginPopupContextWindow("HierarchyContext", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Cube")) {
                uint64_t newId = CreateCubeEntity(scene);
                Entity* e = scene.FindEntity(newId);
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            if (ImGui::MenuItem("Sphere")) {
                uint64_t newId = CreateSphereEntity(scene);
                Entity* e = scene.FindEntity(newId);
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            if (ImGui::MenuItem("Capsule")) {
                uint64_t newId = CreateCapsuleEntity(scene);
                Entity* e = scene.FindEntity(newId);
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            if (ImGui::MenuItem("Plane")) {
                uint64_t newId = CreatePlaneEntity(scene);
                Entity* e = scene.FindEntity(newId);
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            if (ImGui::MenuItem("Triangle")) {
                uint64_t newId = CreateTriangleEntity(scene);
                Entity* e = scene.FindEntity(newId);
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            if (ImGui::MenuItem("Octagon")) {
                uint64_t newId = CreateOctagonEntity(scene);
                Entity* e = scene.FindEntity(newId);
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            if (ImGui::MenuItem("Snowman")) {
                uint64_t newId = CreateSnowmanEntity(scene);
                Entity* e = scene.FindEntity(newId);
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            if (ImGui::MenuItem("Directional Light")) {
                uint64_t newId = scene.CreateEntity("Directional Light");
                Entity* e = scene.FindEntity(newId);
                e->SetFlag(ENTITY_LIGHT);
                e->light.type = LightType::Directional;
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            if (ImGui::MenuItem("Point Light")) {
                uint64_t newId = scene.CreateEntity("Point Light");
                Entity* e = scene.FindEntity(newId);
                e->SetFlag(ENTITY_LIGHT);
                e->light.type = LightType::Point;
                e->light.direction[0] = 0.0f;
                e->light.direction[1] = 0.0f;
                e->light.direction[2] = 0.0f;
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            if (ImGui::MenuItem("Camera")) {
                uint64_t newId = scene.CreateEntity("Camera");
                Entity* e = scene.FindEntity(newId);
                e->SetFlag(ENTITY_CAMERA);
                e->camera.isActive = true;
                // Deactivate other cameras
                for (auto& other : scene.All())
                    if (other.id != newId && other.HasFlag(ENTITY_CAMERA))
                        other.camera.isActive = false;
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Script (Empty)")) {
                uint64_t newId = scene.CreateEntity("Script");
                Entity* e = scene.FindEntity(newId);
                e->SetFlag(ENTITY_SCRIPT);
                e->scriptPath = "";
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            if (ImGui::MenuItem("Model (Group)")) {
                uint64_t newId = scene.CreateEntity("Model");
                Entity* e = scene.FindEntity(newId);
                e->SetFlag(ENTITY_MODEL);
                scene.ClearRedo();
                scene.PushUndo(UndoEntry::Removed, *e);
                selectedEntity = newId;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("ServerService")) {
                // Check if one already exists
                if (scene.FindServerService()) {
                    MessageBoxA(Window::Handle(), "A ServerService already exists in the scene.", "Info", MB_OK);
                } else {
                    uint64_t newId = scene.CreateEntity("ServerService");
                    Entity* e = scene.FindEntity(newId);
                    e->SetFlag(ENTITY_SERVER_SERVICE);
                    scene.ClearRedo();
                    scene.PushUndo(UndoEntry::Removed, *e);
                    selectedEntity = newId;
                }
            }
            if (ImGui::MenuItem("WorldEnvironment")) {
                if (scene.FindWorldEnvironment()) {
                    MessageBoxA(Window::Handle(), "A WorldEnvironment already exists in the scene.", "Info", MB_OK);
                } else {
                    uint64_t newId = scene.CreateEntity("WorldEnvironment");
                    Entity* e = scene.FindEntity(newId);
                    e->SetFlag(ENTITY_WORLD_ENV);
                    scene.ClearRedo();
                    scene.PushUndo(UndoEntry::Removed, *e);
                    selectedEntity = newId;
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Import SDKMESH")) {
    if (ImportSdkMeshToScene(scene)) {
        auto& all = scene.All();
        if (!all.empty()) {
            scene.ClearRedo();
            scene.PushUndo(UndoEntry::Removed, all.back());
            selectedEntity = all.back().id;
        }
    }
}

        if (ImGui::MenuItem("Import OBJ")) {
            if (ImportOBJToScene(scene)) {
                auto& all = scene.All();
                if (!all.empty()) {
                    scene.ClearRedo();
                    scene.PushUndo(UndoEntry::Removed, all.back());
                    selectedEntity = all.back().id;
                }
            }
        }
        ImGui::EndPopup();
    }

    // Check if right-click on hierarchy window should not propagate
    deselect = ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0) && !ImGui::IsAnyItemHovered();

    ImGui::End();
}

bool ShowPropertiesWindow(Entity* entity, Scene& scene)
{
    if (!entity) return false;

    bool rebuilt = false;

    ImGui::SetNextWindowPos(ImVec2(1103, 269), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(260, 434), ImGuiCond_FirstUseEver);
    ImGui::Begin("Properties", nullptr);

    // Name
    char nameBuf[256];
    strncpy(nameBuf, entity->name.c_str(), sizeof(nameBuf));
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        entity->name = nameBuf;
    }
    ImGui::Separator();

    // Services: only show their own section
    if (entity->HasFlag(ENTITY_WORLD_ENV) || entity->HasFlag(ENTITY_SERVER_SERVICE)) {
        // (WorldEnvironment and ServerService sections are drawn at the bottom)
    } else {
        // Transform
        ImGui::TextUnformatted("Transform");
        ImGui::DragFloat3("Position", &entity->transform.position.x, 0.1f);
        ImGui::DragFloat3("Scale",    &entity->transform.scale.x, 0.1f);
        ImGui::DragFloat3("Rotation (rad)", &entity->transform.rotation.x, 0.01f);
        ImGui::Separator();
    }

    // Physics
    if (!entity->HasFlag(ENTITY_LIGHT) && !entity->HasFlag(ENTITY_CAMERA) && !entity->HasFlag(ENTITY_WORLD_ENV) && !entity->HasFlag(ENTITY_SERVER_SERVICE)) {
        ImGui::TextUnformatted("Physics (Bullet)");
        if (ImGui::Checkbox("Simulated", &entity->physics.enabled)) {
            if (entity->physics.enabled && !entity->vertices.empty()) {
                entity->physics.velocity = {0,0,0};
            }
        }
        if (entity->physics.enabled) {
            ImGui::DragFloat("Mass", &entity->physics.mass, 0.1f, 0.0f, 1000.0f);
            ImGui::SliderFloat("Restitution", &entity->physics.restitution, 0.0f, 1.0f);
            ImGui::SliderFloat("Friction", &entity->physics.friction, 0.0f, 1.0f);
        }
        ImGui::Checkbox("Collidable", &entity->physics.collidable);
        ImGui::Separator();
    }

    // Spinning (not shown for lights, services, or WorldEnvironment)
    if (!entity->HasFlag(ENTITY_LIGHT) && !entity->HasFlag(ENTITY_WORLD_ENV) && !entity->HasFlag(ENTITY_SERVER_SERVICE)) {
        ImGui::TextUnformatted("Spinning");
        ImGui::Checkbox("Enabled##spin", &entity->spinning.enabled);
        ImGui::SliderFloat("Speed##spin", &entity->spinning.speed, 0.0f, 5.0f);
        ImGui::Separator();

        // Instancing toggle
        ImGui::TextUnformatted("Instancing");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Instancing for repeated identical models to this object, reduces draw calls when turned on.");
        ImGui::Checkbox("Enabled##inst", &entity->instancingEnabled);
        ImGui::Separator();

        // Face Colors (only show for cube meshes with 24 verts * 6 = 144 floats per vert)
        int vertCount = (int)entity->vertices.size() / VERTEX_STRIDE;

        // Color-undo state persists across frames and entities
        static uint64_t s_ColorUndoId = 0;
        static Entity s_ColorPreEdit(0, "");
        static bool s_AnyColorChanged = false;

        // Reset stale undo state when we leave a cube entity without committing
        if (vertCount != 24) {
            s_ColorUndoId = 0;
            s_AnyColorChanged = false;
        }

        if (vertCount == 24) {
            ImGui::TextUnformatted("Face Colors");
            const char* faceNames[] = { "Front", "Back", "Left", "Right", "Top", "Bottom" };

            // Push undo on first color edit activation
            bool colorActive = false;

            for (int f = 0; f < 6; f++) {
                ImGui::PushID(f);
                float col[3] = {
                    entity->faceColors.colors[f][0],
                    entity->faceColors.colors[f][1],
                    entity->faceColors.colors[f][2],
                };
                bool colorChanged = ImGui::ColorEdit3(faceNames[f], col, ImGuiColorEditFlags_NoInputs);
                if (ImGui::IsItemActivated() && s_ColorUndoId != entity->id) {
                    s_ColorUndoId = entity->id;
                    s_ColorPreEdit = *entity;
                }
                if (ImGui::IsItemActive()) colorActive = true;
                if (colorChanged) {
                    entity->faceColors.colors[f][0] = col[0];
                    entity->faceColors.colors[f][1] = col[1];
                    entity->faceColors.colors[f][2] = col[2];
                    s_AnyColorChanged = true;
                }
                ImGui::PopID();
            }

            if (!colorActive) {
                if (s_ColorUndoId != 0 && s_AnyColorChanged) {
                    scene.ClearRedo();
                    scene.PushUndo(UndoEntry::Modified, s_ColorPreEdit);
                }
                s_ColorUndoId = 0;
                s_AnyColorChanged = false;
            }

            if (s_AnyColorChanged) {
                BuildCubeMesh(entity->faceColors.colors, entity->vertices, entity->indices);
                entity->meshDirty = true;
                rebuilt = true;
            }
        } else if (vertCount > 0) {
            ImGui::Text("Mesh: %d vertices, %zu indices", vertCount, entity->indices.size());
        }
    }

    // Light properties
    if (entity->HasFlag(ENTITY_LIGHT)) {
        ImGui::Separator();
        ImGui::TextUnformatted("Light");

        const char* types[] = { "Directional", "Point" };
        int cur = (entity->light.type == LightType::Point) ? 1 : 0;
        if (ImGui::Combo("Type", &cur, types, 2)) {
            entity->light.type = (cur == 0) ? LightType::Directional : LightType::Point;
        }

        ImGui::ColorEdit3("Color", entity->light.color, ImGuiColorEditFlags_NoInputs);
        ImGui::DragFloat("Intensity", &entity->light.intensity, 0.1f, 0.0f, 100.0f);

        if (entity->light.type == LightType::Directional) {
            ImGui::DragFloat3("Direction", entity->light.direction, 0.1f);
        } else {
            ImGui::DragFloat("Range", &entity->light.range, 0.5f, 0.1f, 500.0f);
        }
    }

    // Camera properties
    if (entity->HasFlag(ENTITY_CAMERA)) {
        ImGui::Separator();
        ImGui::TextUnformatted("Camera");
        ImGui::Checkbox("Active", &entity->camera.isActive);
        ImGui::DragFloat("FOV", &entity->camera.fov, 1.0f, 1.0f, 179.0f, "%.0f");
        ImGui::DragFloat("Near", &entity->camera.nearPlane, 0.1f, 0.01f, 10.0f);
        ImGui::DragFloat("Far", &entity->camera.farPlane, 1.0f, 1.0f, 1000.0f);

        // Camera view preview placeholder (rendered in main.cpp)
        ImGui::Separator();
        ImGui::TextUnformatted("Camera View");
        float previewW = ImGui::GetContentRegionAvail().x;
        float previewH = previewW * 9.0f / 16.0f;
        if (entity->camera.isActive) {
            ImGui::TextColored(ImVec4(0,1,0,1), "Active camera - viewport shows this view");
        } else {
            ImGui::TextUnformatted("Set Active to render this camera's view");
        }
        // Placeholder rect showing camera aspect
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + previewW, p0.y + previewH);
        dl->AddRectFilled(p0, p1, IM_COL32(20, 20, 30, 200));
        dl->AddRect(p0, p1, IM_COL32(100, 100, 120, 255));
        ImGui::Dummy(ImVec2(previewW, previewH));
        ImGui::SetCursorScreenPos(ImVec2(p0.x + previewW*0.5f - 40, p0.y + previewH*0.5f - 8));
        ImGui::Text("Camera View");
    }

    // Script properties (4xLang v0.1)
    if (entity->HasFlag(ENTITY_SCRIPT)) {
        ImGui::Separator();
        ImGui::TextUnformatted("4xLang Script");

        char pathBuf[512];
        strncpy(pathBuf, entity->scriptPath.c_str(), sizeof(pathBuf));
        pathBuf[sizeof(pathBuf) - 1] = '\0';
        ImGui::InputText("Path", pathBuf, sizeof(pathBuf));

        ImGui::SameLine();
        if (ImGui::Button("...")) {
            char openPath[MAX_PATH] = {};
            if (OpenScriptFileDialog(openPath, MAX_PATH)) {
                entity->scriptPath = openPath;
                pathBuf[0] = '\0';
                strncpy(pathBuf, openPath, sizeof(pathBuf));
            }
        }

        if (ImGui::Button("Load Script") && !entity->scriptPath.empty()) {
            if (g_ScriptEngine) {
                // Unload previous if any
                if (entity->scriptHandle >= 0) {
                    g_ScriptEngine->UnloadScript(entity->scriptHandle);
                }
                entity->scriptHandle = g_ScriptEngine->LoadEntityScript(entity->scriptPath, entity->id);
                if (entity->scriptHandle < 0) {
                    MessageBoxA(Window::Handle(), "Failed to load script", "Error", MB_ICONERROR);
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Edit")) {
            Overlay::g_EditorPath = entity->scriptPath;
            Overlay::g_EditingEntity = entity->id;
            Overlay::g_ShowEditor = true;

            if (!entity->scriptPath.empty()) {
                std::string content = ReadFileToString(entity->scriptPath);
                g_Editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
                g_Editor.SetPalette(TextEditor::GetRetroBluePalette());
                g_Editor.SetText(content);
            } else {
                g_Editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
                g_Editor.SetPalette(TextEditor::GetRetroBluePalette());
                g_Editor.SetText("-- 4xLang v0.1\n-- New script\n\nfunction update(dt)\n    -- your code here\nend\n");
            }
            g_EditorTextChanged = false;
        }

        if (!entity->scriptPath.empty()) {
            ImGui::Text("File: %s", GetFileName(entity->scriptPath).c_str());
        }
        if (entity->scriptHandle >= 0) {
            ScriptInstance* si = g_ScriptEngine ? g_ScriptEngine->GetScript(entity->scriptHandle) : nullptr;
            if (si && si->hasError) {
                ImGui::TextColored(ImVec4(1,0,0,1), "Error: %s", si->errorMsg.c_str());
            } else if (si) {
                ImGui::TextColored(ImVec4(0,1,0,1), "Running");
            }
        }
    }

    // ServerService properties
    if (entity->HasFlag(ENTITY_SERVER_SERVICE)) {
        ImGui::Separator();
        ImGui::TextUnformatted("ServerService (4xLang v0.1)");
        ImGui::TextWrapped("Server scripts run with global access to all entities.");
        ImGui::TextWrapped("Drag .scr / .4xs script files into the hierarchy under this service.");

        int childCount = 0;
        for (auto& e : scene.All()) {
            if (e.parentId == entity->id) childCount++;
        }
        ImGui::Text("Children: %d", childCount);

        if (ImGui::Button("Open ServerService Window")) {
            Overlay::g_EditingEntity = entity->id;
        }
    }

    // WorldEnvironment properties
    if (entity->HasFlag(ENTITY_WORLD_ENV)) {
        ImGui::Separator();
        ImGui::TextUnformatted("WorldEnvironment");
        ImGui::Separator();

        // Time display (12h format)
        {
            int hours12 = (int)entity->worldEnv.timeOfDay % 12;
            if (hours12 == 0) hours12 = 12;
            int minutes = (int)((entity->worldEnv.timeOfDay - (int)entity->worldEnv.timeOfDay) * 60.0f);
            bool isPM = entity->worldEnv.timeOfDay >= 12.0f;
            char timeBuf[32];
            snprintf(timeBuf, sizeof(timeBuf), "%d:%02d %s", hours12, minutes, isPM ? "PM" : "AM");
            ImGui::Text("Time: %s", timeBuf);
            ImGui::SameLine();
            ImGui::PushID("timeSlider");
            if (ImGui::SliderFloat("##time", &entity->worldEnv.timeOfDay, 0.0f, 24.0f, "%.1f")) {
                if (g_Backend == GfxBackend::D3D10)
                    d3d10::Skybox::SetTimeOfDay(entity->worldEnv.timeOfDay);
                else
                    d3d11::Skybox::SetTimeOfDay(entity->worldEnv.timeOfDay);
            }
            ImGui::PopID();
        }

        // Sun brightness (0-100% maps to 0-1)
        float brightness = entity->worldEnv.lightIntensity * 100.0f;
        if (ImGui::SliderFloat("Brightness", &brightness, 0.0f, 100.0f, "%.0f%%")) {
            entity->worldEnv.lightIntensity = brightness / 100.0f;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(sun light intensity)");

        // Skybox color
        ImGui::ColorEdit3("Sky Color", entity->worldEnv.skyColor);

        // Shadows placeholder
        ImGui::Checkbox("Shadows", &entity->worldEnv.shadowsEnabled);
        ImGui::SameLine();
        ImGui::TextDisabled("(coming soon)");

        // Volumetric lighting placeholder
        ImGui::Checkbox("Volumetric Lighting", &entity->worldEnv.volumetricLighting);
        ImGui::SameLine();
        ImGui::TextDisabled("(coming soon)");
    }

    ImGui::End();
    return rebuilt;
}

void ShowGizmo(const float* view, const float* projection, Entity* entity, Scene& scene)
{
    if (!entity) return;

    using namespace DirectX;
    XMMATRIX s = XMMatrixScaling(entity->transform.scale.x, entity->transform.scale.y, entity->transform.scale.z);
    XMMATRIX r = XMMatrixRotationRollPitchYaw(entity->transform.rotation.x, entity->transform.rotation.y, entity->transform.rotation.z);
    XMMATRIX t = XMMatrixTranslation(entity->transform.position.x, entity->transform.position.y, entity->transform.position.z);
    XMMATRIX world = s * r * t;

    float matrix[16];
    memcpy(matrix, &world, sizeof(matrix));

    float oldPos[3] = { entity->transform.position.x, entity->transform.position.y, entity->transform.position.z };
    float oldScale[3] = { entity->transform.scale.x, entity->transform.scale.y, entity->transform.scale.z };
    float oldRot[3] = { entity->transform.rotation.x, entity->transform.rotation.y, entity->transform.rotation.z };

    ImGui::SetNextWindowPos(ImVec2(806, 22), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(198, 35), ImGuiCond_FirstUseEver);
    ImGui::Begin("Gizmo Controls", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize);
    if (ImGui::RadioButton("T", g_Gizmo.GetOperation() == Gizmo::TRANSLATE)) g_Gizmo.SetOperation(Gizmo::TRANSLATE);
    ImGui::SameLine();
    if (ImGui::RadioButton("R", g_Gizmo.GetOperation() == Gizmo::ROTATE)) g_Gizmo.SetOperation(Gizmo::ROTATE);
    ImGui::SameLine();
    if (ImGui::RadioButton("S", g_Gizmo.GetOperation() == Gizmo::SCALE)) g_Gizmo.SetOperation(Gizmo::SCALE);
    ImGui::SameLine();
    if (ImGui::RadioButton("L", g_Gizmo.GetMode() == Gizmo::LOCAL)) g_Gizmo.SetMode(Gizmo::LOCAL);
    ImGui::SameLine();
    if (ImGui::RadioButton("W", g_Gizmo.GetMode() == Gizmo::WORLD)) g_Gizmo.SetMode(Gizmo::WORLD);
    ImGui::End();

    g_Gizmo.SetActiveEntity(entity->id);
    g_Gizmo.SetRect(0, 0, (float)Window::Width(), (float)Window::Height());

    int vertCount = (int)entity->vertices.size() / VERTEX_STRIDE;
    const float* vertData = entity->vertices.empty() ? nullptr : entity->vertices.data();
    g_Gizmo.Manipulate(view, projection, matrix,
                       vertData, 9, vertCount,
                       g_Gizmo.GetOperation(), g_Gizmo.GetMode());

    if (g_Gizmo.IsUsing()) {
        // Decompose matrix: scale = length of basis vectors
        float right[3] = { matrix[0], matrix[1], matrix[2] };
        float up[3] = { matrix[4], matrix[5], matrix[6] };
        float dir[3] = { matrix[8], matrix[9], matrix[10] };

        float sx = sqrtf(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
        float sy = sqrtf(up[0]*up[0] + up[1]*up[1] + up[2]*up[2]);
        float sz = sqrtf(dir[0]*dir[0] + dir[1]*dir[1] + dir[2]*dir[2]);

        entity->transform.position.x = matrix[12];
        entity->transform.position.y = matrix[13];
        entity->transform.position.z = matrix[14];

        entity->transform.scale.x = sx;
        entity->transform.scale.y = sy;
        entity->transform.scale.z = sz;

        // Normalize basis to extract rotation
        if (sx > 1e-10f) { right[0] /= sx; right[1] /= sx; right[2] /= sx; }
        if (sy > 1e-10f) { up[0] /= sy; up[1] /= sy; up[2] /= sy; }
        if (sz > 1e-10f) { dir[0] /= sz; dir[1] /= sz; dir[2] /= sz; }

        // Extract Euler angles for R = Ry(yaw) * Rx(pitch) * Rz(roll)
        float cp = sqrtf(dir[0]*dir[0] + dir[2]*dir[2]);
        if (cp > 1e-6f) {
            entity->transform.rotation.x = atan2f(-dir[1], cp);
            entity->transform.rotation.y = atan2f(dir[0], dir[2]);
            entity->transform.rotation.z = atan2f(right[1], up[1]);
        } else {
            entity->transform.rotation.x = atan2f(-dir[1], 0.0f);
            entity->transform.rotation.y = 0.0f;
            entity->transform.rotation.z = atan2f(-up[0], right[0]);
        }

        // Propagate transform delta to all children
        float dx = entity->transform.position.x - oldPos[0];
        float dy = entity->transform.position.y - oldPos[1];
        float dz = entity->transform.position.z - oldPos[2];
        float dsx = (oldScale[0] != 0.0f) ? entity->transform.scale.x / oldScale[0] : 1.0f;
        float dsy = (oldScale[1] != 0.0f) ? entity->transform.scale.y / oldScale[1] : 1.0f;
        float dsz = (oldScale[2] != 0.0f) ? entity->transform.scale.z / oldScale[2] : 1.0f;
        float drx = entity->transform.rotation.x - oldRot[0];
        float dry = entity->transform.rotation.y - oldRot[1];
        float drz = entity->transform.rotation.z - oldRot[2];

        auto children = scene.GetChildren(entity->id);
        for (auto* child : children) {
            child->transform.position.x += dx;
            child->transform.position.y += dy;
            child->transform.position.z += dz;
            child->transform.scale.x *= dsx;
            child->transform.scale.y *= dsy;
            child->transform.scale.z *= dsz;
            child->transform.rotation.x += drx;
            child->transform.rotation.y += dry;
            child->transform.rotation.z += drz;
        }
    }
}

bool IsGizmoOver() {
    return g_Gizmo.IsOver();
}

void DrawLightIcons(Scene& scene, const float* view, const float* projection, int viewportW, int viewportH)
{
    using namespace DirectX;
    XMMATRIX vp = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(view)) *
                   XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(projection));
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (auto& ent : scene.All()) {
        if (!ent.HasFlag(ENTITY_LIGHT)) continue;
        XMVECTOR wp = XMVectorSet(ent.transform.position.x, ent.transform.position.y, ent.transform.position.z, 1.0f);
        XMVECTOR sp = XMVector4Transform(wp, vp);
        float w = XMVectorGetW(sp);
        if (fabsf(w) < 1e-10f) continue;
        if (w < 0.0f) continue;
        float sx = (XMVectorGetX(sp) / w + 1.0f) * 0.5f * viewportW;
        float sy = (-XMVectorGetY(sp) / w + 1.0f) * 0.5f * viewportH;
        float r = 12.0f;
        ImU32 yellow = IM_COL32(255, 220, 50, 220);
        ImU32 rays   = IM_COL32(255, 200, 30, 120);
        dl->AddCircle(ImVec2(sx, sy), r, yellow, 0, 2.5f);
        dl->AddCircleFilled(ImVec2(sx, sy), r - 3, yellow);
        dl->AddLine(ImVec2(sx - r - 4, sy), ImVec2(sx + r + 4, sy), rays, 1.5f);
        dl->AddLine(ImVec2(sx, sy - r - 4), ImVec2(sx, sy + r + 4), rays, 1.5f);
        dl->AddLine(ImVec2(sx - r - 2, sy - r - 2), ImVec2(sx + r + 2, sy + r + 2), rays, 1.0f);
        dl->AddLine(ImVec2(sx + r + 2, sy - r - 2), ImVec2(sx - r - 2, sy + r + 2), rays, 1.0f);
    }
}

void DrawCameraIcons(Scene& scene, const float* view, const float* projection, int viewportW, int viewportH)
{
    using namespace DirectX;
    XMMATRIX vp = XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(view)) *
                   XMLoadFloat4x4(reinterpret_cast<const XMFLOAT4X4*>(projection));
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    for (auto& ent : scene.All()) {
        if (!ent.HasFlag(ENTITY_CAMERA)) continue;
        XMVECTOR wp = XMVectorSet(ent.transform.position.x, ent.transform.position.y, ent.transform.position.z, 1.0f);
        XMVECTOR sp = XMVector4Transform(wp, vp);
        float w = XMVectorGetW(sp);
        if (fabsf(w) < 1e-10f) continue;
        if (w < 0.0f) continue;
        float sx = (XMVectorGetX(sp) / w + 1.0f) * 0.5f * viewportW;
        float sy = (-XMVectorGetY(sp) / w + 1.0f) * 0.5f * viewportH;
        ImU32 white = IM_COL32(255, 255, 255, 220);
        ImU32 fill  = IM_COL32(60, 60, 60, 160);
        float r = 14.0f;
        // Camera body: trapezoid
        ImVec2 pts[4] = {
            ImVec2(sx - r*0.6f, sy + r*0.5f),
            ImVec2(sx + r*0.6f, sy + r*0.5f),
            ImVec2(sx + r*0.8f, sy - r*0.3f),
            ImVec2(sx - r*0.8f, sy - r*0.3f),
        };
        dl->AddConvexPolyFilled(pts, 4, fill);
        dl->AddPolyline(pts, 4, white, 1.5f);
        // Lens
        dl->AddCircle(ImVec2(sx, sy - r*0.1f), r*0.35f, white, 0, 1.5f);
        dl->AddCircleFilled(ImVec2(sx, sy - r*0.1f), r*0.25f, IM_COL32(120, 180, 255, 180));
        // Viewfinder bump on top
        dl->AddRectFilled(ImVec2(sx - r*0.25f, sy - r*0.5f), ImVec2(sx + r*0.25f, sy - r*0.35f), fill);
        dl->AddRect(ImVec2(sx - r*0.25f, sy - r*0.5f), ImVec2(sx + r*0.25f, sy - r*0.35f), white);
    }
}

// ── Scene Save ──
void SaveSceneToProject(Scene& scene)
{
    if (ProjectManagerUI::g_ProjectFolder.empty()) return;
    std::string sceneFile = ProjectManagerUI::g_ProjectFolder + "\\scenes\\default.gaf";
    WriteGAF(sceneFile.c_str(), scene.All());
    scene.ClearModified();
}

// ── Script Editor Window (4xLang v0.1) ──
static void SaveScript(Scene& scene)
{
    std::string path = g_EditorPath;

    if (path.empty()) {
        // Auto-generate path in project scripts folder
        std::string scriptsDir = ProjectManagerUI::g_ProjectFolder.empty()
            ? "" : ProjectManager::GetScriptsFolder(ProjectManagerUI::g_ProjectFolder);
        if (scriptsDir.empty()) return;

        // Use entity name if available, otherwise use entity ID
        std::string name;
        if (g_EditingEntity != 0) {
            Entity* e = scene.FindEntity(g_EditingEntity);
            if (e) name = e->name;
        }
        if (name.empty()) {
            char buf[64];
            snprintf(buf, sizeof(buf), "script_%llu", (unsigned long long)g_EditingEntity);
            name = buf;
        }
        path = scriptsDir + "\\" + name + ".scr";
    }

    std::string newContent = g_Editor.GetText();
    if (!WriteStringToFile(path, newContent)) return;

    g_EditorPath = path;
    g_EditorTextChanged = false;

    // Update entity's script path if editing an entity script
    if (g_EditingEntity != 0) {
        Entity* e = scene.FindEntity(g_EditingEntity);
        if (e) {
            e->scriptPath = path;
            // Reload script in engine
            if (g_ScriptEngine) {
                if (e->scriptHandle >= 0)
                    g_ScriptEngine->UnloadScript(e->scriptHandle);
                Entity* sv = scene.FindServerService();
                if (sv && e->parentId == sv->id)
                    e->scriptHandle = g_ScriptEngine->LoadServerScript(path);
                else
                    e->scriptHandle = g_ScriptEngine->LoadEntityScript(path, e->id);
            }
        }
    } else {
        // Standalone save — reload by path if already loaded
        if (g_ScriptEngine) {
            int h = g_ScriptEngine->FindScriptByPath(path);
            if (h >= 0) g_ScriptEngine->ReloadScript(h);
        }
    }

    // Also trigger a project refresh: save the scene
    if (!ProjectManagerUI::g_ProjectFolder.empty()) {
        std::string sceneFile = ProjectManagerUI::g_ProjectFolder + "\\scenes\\default.gaf";
        WriteGAF(sceneFile.c_str(), scene.All());
    }
}

void ShowEditorWindow(Scene& scene, Entity* entity)
{
    if (!g_ShowEditor) return;

    ImGui::SetNextWindowPos(ImVec2(356, 26), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(514, 336), ImGuiCond_FirstUseEver);
    ImGui::Begin("4xLang Code Editor", &g_ShowEditor, ImGuiWindowFlags_MenuBar);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save (Ctrl+S)")) {
                SaveScript(scene);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    if (!g_EditorPath.empty()) {
        ImGui::Text("Editing: %s", g_EditorPath.c_str());
    } else {
        ImGui::Text("Editing: <new script>");
    }

    if (g_EditorTextChanged) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1,1,0,1), " [Modified]");
    }

    ImGui::Separator();

    ImVec2 editorSize = ImGui::GetContentRegionAvail();
    g_Editor.Render("##4xEditor", editorSize, false);

    if (g_Editor.IsTextChanged()) {
        g_EditorTextChanged = true;
    }

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        SaveScript(scene);
    }

    ImGui::End();
}

// ── Debug Console (4xLang v0.1) ──
void ShowDebugConsole(bool* pOpen)
{
    ImGui::SetNextWindowPos(ImVec2(0, 516), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600, 184), ImGuiCond_FirstUseEver);
    ImGui::Begin("4xLang Debug Console", pOpen);

    // Toolbar
    static char filterBuf[128] = "";
    ImGui::SetNextItemWidth(180);
    ImGui::InputText("Filter", filterBuf, sizeof(filterBuf));
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        ClearConsole();
    }
    ImGui::SameLine();
    if (ImGui::Button("Export")) {
        char exportPath[MAX_PATH] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = Window::Handle();
        ofn.lpstrFilter = "Log Files\0*.log\0All Files\0*.*\0";
        ofn.lpstrFile = exportPath;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = "log";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        exportPath[0] = '\0';
        if (GetSaveFileNameA(&ofn)) {
            std::ofstream f(exportPath);
            if (f) {
                for (auto& entry : g_ConsoleLog) {
                    int h = (int)(entry.timestamp / 3600) % 24;
                    int m = (int)(entry.timestamp / 60) % 60;
                    int s = (int)entry.timestamp % 60;
                    char prefix = (entry.type == CONSOLE_INFO) ? 'I' : (entry.type == CONSOLE_WARN) ? 'W' : 'E';
                    f << prefix << " " << h << ":" << m << ":" << s << " " << entry.text << "\n";
                }
            }
        }
    }
    ImGui::Separator();

    // Console entries
    ImGui::BeginChild("##consoleScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    std::string filter = filterBuf;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    for (auto& entry : g_ConsoleLog) {
        if (!filter.empty()) {
            std::string lower = entry.text;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (lower.find(filter) == std::string::npos)
                continue;
        }
        int h = (int)(entry.timestamp / 3600) % 24;
        int m = (int)(entry.timestamp / 60) % 60;
        int s = (int)entry.timestamp % 60;
        char timeBuf[32];
        snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", h, m, s);

        ImVec4 color;
        if (entry.type == CONSOLE_ERROR)      color = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        else if (entry.type == CONSOLE_WARN)  color = ImVec4(1.0f, 0.9f, 0.3f, 1.0f);
        else                                  color = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);

        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "%s", timeBuf);
        ImGui::SameLine();
        ImGui::TextColored(color, "%s", entry.text.c_str());
    }
    if (!g_ConsoleLog.empty() && filter.empty() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f)
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    ImGui::End();
}

// ── ServerService Window (4xLang v0.1) ──
void ShowServerServiceWindow(Scene& scene)
{
    Entity* sv = scene.FindServerService();
    if (!sv) return;

    ImGui::SetNextWindowPos(ImVec2(2, 283), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(297, 221), ImGuiCond_FirstUseEver);
    ImGui::Begin("ServerService Manager", nullptr);

    ImGui::TextUnformatted("Server scripts (global scope):");
    ImGui::Separator();

    // List script children of the ServerService
    int idx = 0;
    for (auto& e : scene.All()) {
        if (e.parentId == sv->id) {
            ImGui::PushID(idx++);
            std::string label = (e.HasFlag(ENTITY_SCRIPT)) ? "[Scr] " : "[?] ";
            label += e.name;

            bool selected = (e.id == Overlay::g_EditingEntity);
            if (ImGui::Selectable(label.c_str(), selected)) {
                Overlay::g_EditingEntity = e.id;
                Overlay::g_EditorPath = e.scriptPath;
                Overlay::g_ShowEditor = true;

                std::string content = ReadFileToString(e.scriptPath);
                g_Editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
                g_Editor.SetPalette(TextEditor::GetRetroBluePalette());
                g_Editor.SetText(content);
                g_EditorTextChanged = false;
            }

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_NODE")) {
                    uint64_t dragId = *(const uint64_t*)payload->Data;
                    Entity* dragged = scene.FindEntity(dragId);
                    if (dragged && dragged->HasFlag(ENTITY_SCRIPT)) {
                        scene.ReparentEntity(dragId, sv->id);
                    }
                    // Non-script entities are silently ignored (user said they just get ignored)
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::PopID();
        }
    }

    // Drop target at bottom for adding new scripts
    ImGui::Separator();
    ImGui::TextUnformatted("Drop .scr / .4xs scripts here or click to add:");
    if (ImGui::Button("Add Script File")) {
        char openPath[MAX_PATH] = {};
        // Default to project scripts folder if available
        std::string defaultDir = ProjectManagerUI::g_ProjectFolder.empty()
            ? "" : ProjectManager::GetScriptsFolder(ProjectManagerUI::g_ProjectFolder);

        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = Window::Handle();
        ofn.lpstrFilter = "4xLang Scripts\0*.scr;*.4xs\0All Files\0*.*\0";
        ofn.lpstrFile = openPath;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrInitialDir = defaultDir.empty() ? nullptr : defaultDir.c_str();
        ofn.lpstrDefExt = "4xs";
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
        openPath[0] = '\0';
        if (!GetOpenFileNameA(&ofn)) { ImGui::End(); return; }

        std::string fileName = GetFileName(openPath);
        uint64_t newId = scene.CreateEntity(fileName);
        Entity* scriptEnt = scene.FindEntity(newId);
        if (scriptEnt) {
            scriptEnt->SetFlag(ENTITY_SCRIPT);
            scriptEnt->scriptPath = openPath;
            scriptEnt->parentId = sv->id;
            scene.ClearRedo();
            scene.PushUndo(UndoEntry::Removed, *scriptEnt);

            // Load into engine
            if (g_ScriptEngine) {
                scriptEnt->scriptHandle = g_ScriptEngine->LoadServerScript(openPath);
            }

            // Open in editor
            Overlay::g_EditingEntity = newId;
            Overlay::g_EditorPath = openPath;
            Overlay::g_ShowEditor = true;
            std::string content = ReadFileToString(openPath);
            g_Editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
            g_Editor.SetPalette(TextEditor::GetRetroBluePalette());
            g_Editor.SetText(content);
            g_EditorTextChanged = false;
        }
    }

    ImGui::End();
}

// ── File > Export ──
static bool ExportProject(Scene& scene)
{
    // Folder picker
    wchar_t folder[MAX_PATH] = {};
    BROWSEINFOW bi = {};
    bi.lpszTitle = L"Select export folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (!pidl) return false;
    if (!SHGetPathFromIDListW(pidl, folder)) { CoTaskMemFree(pidl); return false; }
    CoTaskMemFree(pidl);

    // Check that at least one camera entity exists
    bool hasCamera = false;
    for (auto& e : scene.All())
        if (e.HasFlag(ENTITY_CAMERA)) { hasCamera = true; break; }
    if (!hasCamera) {
        MessageBoxA(Window::Handle(), "Cannot export: no camera entity in the scene.\nAdd a Camera entity (right-click hierarchy > Camera) before exporting.", "Export Error", MB_ICONERROR);
        return false;
    }

    char folderA[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, folder, -1, folderA, MAX_PATH, nullptr, nullptr);

    // Write new archive format (.pak + index)
    if (!ExportArchive(folderA, scene.All())) {
        // Fallback to legacy GAF
        char gafPath[MAX_PATH];
        snprintf(gafPath, sizeof(gafPath), "%s\\data.gaf", folderA);
        if (!WriteGAF(gafPath, scene.All())) {
            MessageBoxA(Window::Handle(), "Failed to export game data", "Export Error", MB_ICONERROR);
            return false;
        }
    }

    // Copy pre-built game.exe (built alongside main.exe via build.bat)
    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *lastSlash = '\0';

    char srcExe[MAX_PATH];
    snprintf(srcExe, sizeof(srcExe), "%s\\game.exe", exeDir);
    char dstExe[MAX_PATH];
    snprintf(dstExe, sizeof(dstExe), "%s\\game.exe", folderA);

    if (!CopyFileA(srcExe, dstExe, FALSE)) {
        MessageBoxA(Window::Handle(), "Failed to copy game.exe.\nRun build.bat first to build both main.exe and game.exe.", "Export Error", MB_ICONERROR);
        return false;
    }

    // Copy DLLs alongside game.exe (loader needs them before main() runs)
    const char* dlls[] = {
        "libzstd.dll",
        "libstdc++-6.dll",
        "libgcc_s_seh_64-1.dll",
        nullptr
    };
    for (int i = 0; dlls[i]; i++) {
        char dst[MAX_PATH];
        snprintf(dst, sizeof(dst), "%s\\%s", folderA, dlls[i]);
        if (GetFileAttributesA(dst) != INVALID_FILE_ATTRIBUTES) continue;
        char fullPath[MAX_PATH];
        if (SearchPathA(nullptr, dlls[i], nullptr, MAX_PATH, fullPath, nullptr)) {
            CopyFileA(fullPath, dst, FALSE);
        }
    }

    char msg[MAX_PATH + 64];
    snprintf(msg, sizeof(msg), "Project exported to:\n%s\n\nRun game.exe to play.", folderA);
    MessageBoxA(Window::Handle(), msg, "Export Complete", MB_OK);
    return true;
}

bool DrawMenuBar(Scene& scene)
{
    bool triggered = false;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Save Scene (Ctrl+S)")) {
                SaveSceneToProject(scene);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export")) {
                triggered = ExportProject(scene);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                SaveSceneToProject(scene);
                PostQuitMessage(0);
            }
            ImGui::EndMenu();
        }

        // Options menu
        if (ImGui::BeginMenu("Options")) {
            if (ImGui::MenuItem("Save Theme...")) {
                SaveThemeToINI();
            }
            ImGui::EndMenu();
        }

        // Play/Stop button
        ImGui::SameLine();
        if (g_PlayMode) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Stop")) {
                g_PlayMode = false;
                g_PlayRequest = true;
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.2f, 1.0f));
            if (ImGui::Button("Play")) {
                g_PlayMode = true;
                g_PlayRequest = true;
            }
            ImGui::PopStyleColor();
        }

        ImGui::EndMainMenuBar();
    }

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) {
        SaveSceneToProject(scene);
    }

    return triggered;
}

void EndFrame(void* context, GfxBackend backend)
{
    ImGui::Render();

    if (backend == GfxBackend::D3D10) {
        if (g_Q10.end) g_Q10.end->End();
        if (g_Q10.disjoint) g_Q10.disjoint->End();
        ImGui_ImplDX10_RenderDrawData(ImGui::GetDrawData());
    } else if (backend == GfxBackend::D3D11) {
        ID3D11DeviceContext* ctx = static_cast<ID3D11DeviceContext*>(context);
        if (g_Q11.end) ctx->End(g_Q11.end.get());
        if (g_Q11.disjoint) ctx->End(g_Q11.disjoint.get());
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }
}

// ── Dockspace stubs (no ImGui docking branch available; windows position manually) ──
void BeginDockspace() {}
void EndDockspace() {}

// ── Helper: enumerate project files (non-recursive, iterative) ──
static void EnumerateProjectFiles(const std::string& proj, std::vector<std::string>& outFiles)
{
    outFiles.clear();
    struct DirEntry {
        std::string path;
        std::string prefix;
        int depth;
    };
    std::vector<DirEntry> stack;
    stack.push_back({ proj, "", 3 });

    while (!stack.empty()) {
        DirEntry de = stack.back();
        stack.pop_back();
        if (de.depth < 0) continue;

        WIN32_FIND_DATAA ffd;
        std::string search = de.path + "\\*";
        HANDLE h = FindFirstFileA(search.c_str(), &ffd);
        if (h == INVALID_HANDLE_VALUE) continue;

        do {
            if (strcmp(ffd.cFileName, ".") == 0 || strcmp(ffd.cFileName, "..") == 0) continue;
            std::string full = de.path + "\\" + ffd.cFileName;
            std::string rel = de.prefix + ffd.cFileName;
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (de.depth > 0)
                    stack.push_back({ full, rel + "/", de.depth - 1 });
            } else {
                outFiles.push_back(rel);
            }
        } while (FindNextFileA(h, &ffd) != 0);
        FindClose(h);
    }

    std::sort(outFiles.begin(), outFiles.end());
}

// ── Directory Manager (bottom panel) ──
void ShowDirectoryManagerWindow()
{
    ImGui::SetNextWindowPos(ImVec2(606, 514), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(490, 189), ImGuiCond_FirstUseEver);
    ImGui::Begin("Directory Manager", nullptr, ImGuiWindowFlags_NoFocusOnAppearing);

    if (ProjectManagerUI::g_ProjectFolder.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "No project open");
        ImGui::End();
        return;
    }

    std::string& proj = ProjectManagerUI::g_ProjectFolder;
    static std::vector<std::string> s_Files;
    static double s_LastRefresh = 0.0;

    double now = ImGui::GetTime();
    if (now - s_LastRefresh > 1.0) {
        s_LastRefresh = now;
        EnumerateProjectFiles(proj, s_Files);
    }

    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_DefaultOpen |
        ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanFullWidth;
    bool rootOpen = ImGui::TreeNodeEx("dir://", rootFlags);
    if (ImGui::IsItemClicked() && ImGui::IsMouseDoubleClicked(0)) {
        ShellExecuteA(nullptr, "open", proj.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (rootOpen) {
        std::string lastDir;
        for (auto& f : s_Files) {
            size_t slash = f.find_last_of("/");
            std::string dirName = (slash == std::string::npos) ? "" : f.substr(0, slash);
            std::string baseName = (slash == std::string::npos) ? f : f.substr(slash + 1);

            if (dirName != lastDir) {
                lastDir = dirName;
                if (dirName.empty()) continue;
                ImGui::TextUnformatted(dirName.c_str());
            }

            const char* icon = " ";
            size_t dot = baseName.rfind('.');
            if (dot != std::string::npos) {
                std::string ext = baseName.substr(dot);
                if (ext == ".scr" || ext == ".4xs") icon = "[S]";
                else if (ext == ".gaf") icon = "[G]";
                else if (ext == ".4pf" || ext == ".4xp") icon = "[P]";
                else if (ext == ".obj" || ext == ".sdkmesh") icon = "[M]";
                else icon = "[F]";
            }

            bool sel = false;
            std::string label = std::string(icon) + " " + baseName;
            ImGui::Selectable(label.c_str(), &sel);

            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                std::string absPath = ProjectManager::DirToAbsolute(proj, "dir://" + f);
                dot = baseName.rfind('.');
                if (dot != std::string::npos) {
                    std::string ext = baseName.substr(dot);
                    if (ext == ".scr" || ext == ".4xs") {
                        Overlay::g_EditorPath = absPath;
                        Overlay::g_EditingEntity = 0;
                        Overlay::g_ShowEditor = true;
                        std::string content = ReadFileToString(absPath);
                        g_Editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
                        g_Editor.SetPalette(TextEditor::GetRetroBluePalette());
                        g_Editor.SetText(content);
                        g_EditorTextChanged = false;
                    }
                }
            }
        }
        ImGui::TreePop();
    }

    ImGui::End();
}

void Shutdown()
{
    g_Q10.disjoint.release();
    g_Q10.start.release();
    g_Q10.end.release();
    g_Q11.disjoint.release();
    g_Q11.start.release();
    g_Q11.end.release();

    if (g_Backend == GfxBackend::D3D10) ImGui_ImplDX10_Shutdown();
    else                                 ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    g_Backend = GfxBackend::None;
}

} // namespace Overlay
