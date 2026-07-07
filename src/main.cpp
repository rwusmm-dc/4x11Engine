#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <stdexcept>
#include <unordered_set>
#include <unordered_map>

#ifdef EDITOR_BUILD
#include "imgui.h"
#endif
#include "core/Window.h"
#include "core/FPSCamera.h"
#include "ecs/ECS.h"
#include "io/Archive.h"
#include "d3d10/Device.h"
#include "d3d10/Pipeline.h"
#include "d3d10/skybox.h"
#include "d3d11/Device.h"
#include "d3d11/Pipeline.h"
#include "d3d11/skybox.h"

#ifdef EDITOR_BUILD
#include "ui/Overlay.h"
#include "ui/ProjectManagerUI.h"
#include "core/Project.h"
#endif
#include "core/CullingSystem.h"
#include "phy/4xPhys.h"
#include "script/ScriptEngine.h"


using namespace DirectX;

static GfxBackend g_Backend = GfxBackend::None;
static FPSCamera  g_Camera;
static bool g_MouseLocked = false;
static int g_LastMX = 0, g_LastMY = 0;

static Scene     g_Scene;
#ifdef EDITOR_BUILD
static uint64_t  g_SelectedEntity = 0;
static bool      g_DeleteKeyHeld = false;
#endif
static bool      g_GameMode = false;
bool g_PlayMode = false;
bool g_PlayRequest = false;
static std::vector<Entity> g_SavedScene; // snapshot before play mode
static CullingSystem g_Culling;
static PhysWorld4X g_4xPhys;
static ScriptEngine* g_ScriptEngineInst = nullptr;

static Entity* GetActiveCamera()
{
    for (auto& e : g_Scene.All())
        if (e.HasFlag(ENTITY_CAMERA) && e.camera.isActive)
            return &e;
    return nullptr;
}

static XMMATRIX GetCameraViewMatrix(const Entity& cam)
{
    XMVECTOR pos = XMLoadFloat3(&cam.transform.position);
    XMVECTOR fwd = XMVectorSet(0, 0, 1, 0);
    XMMATRIX rot = XMMatrixRotationRollPitchYaw(cam.transform.rotation.x,
                                                 cam.transform.rotation.y,
                                                 cam.transform.rotation.z);
    fwd = XMVector3TransformNormal(fwd, rot);
    XMVECTOR target = pos + fwd;
    return XMMatrixLookAtLH(pos, target, XMVectorSet(0, 1, 0, 0));
}

static void HandleCameraInput(float dt)
{
    if (g_GameMode || g_PlayMode || GetActiveCamera()) return;

    bool* keys = Window::Keys();

#ifdef EDITOR_BUILD
    if (Window::MouseDown(1) && !g_MouseLocked && !ImGui::GetIO().WantCaptureMouse) {
#else
    if (Window::MouseDown(1) && !g_MouseLocked) {
#endif
        g_MouseLocked = true;
        g_Camera.LockMouse(true);
        ShowCursor(FALSE);

        RECT rect;
        GetClientRect(Window::Handle(), &rect);
        POINT center = { rect.right / 2, rect.bottom / 2 };
        ClientToScreen(Window::Handle(), &center);
        SetCursorPos(center.x, center.y);

        g_LastMX = rect.right / 2;
        g_LastMY = rect.bottom / 2;

        g_Camera.Update(dt, keys);
        return;
    }
    if ((keys[VK_ESCAPE] || !Window::MouseDown(1)) && g_MouseLocked) {
        g_MouseLocked = false;
        g_Camera.LockMouse(false);
        ShowCursor(TRUE);
    }

    if (g_MouseLocked) {
        int mx = Window::MouseX();
        int my = Window::MouseY();
        int dx = mx - g_LastMX;
        int dy = my - g_LastMY;

        g_Camera.ProcessMouseMove(dx, dy);

        RECT rect;
        GetClientRect(Window::Handle(), &rect);
        if (mx <= 1 || mx >= rect.right - 2 || my <= 1 || my >= rect.bottom - 2) {
            POINT center = { rect.right / 2, rect.bottom / 2 };
            ClientToScreen(Window::Handle(), &center);
            SetCursorPos(center.x, center.y);
            g_LastMX = rect.right / 2;
            g_LastMY = rect.bottom / 2;
        } else {
            g_LastMX = mx;
            g_LastMY = my;
        }
    }

    g_Camera.Update(dt, keys);
}

#ifdef EDITOR_BUILD
static bool g_UndoHeld = false;
static bool g_RedoHeld = false;
static bool g_CopyHeld = false;
static bool g_PasteHeld = false;
static Entity g_CopiedEntity(0, "");

static void DeleteSelectedEntity()
{
    if (g_SelectedEntity == 0) return;

    Entity* e = g_Scene.FindEntity(g_SelectedEntity);
    if (!e) return;

    g_Scene.ClearRedo();
    g_Scene.PushUndo(UndoEntry::Added, *e);

    if (g_Backend == GfxBackend::D3D10)
        d3d10::Pipeline::RemoveEntityMesh(g_SelectedEntity);
    else
        d3d11::Pipeline::RemoveEntityMesh(g_SelectedEntity);

    g_Scene.RemoveEntity(g_SelectedEntity);
    g_SelectedEntity = 0;
}

static std::string MakeUniqueName(Scene& scene, const std::string& base)
{
    std::string name = base;
    int suffix = 1;
    bool exists = true;
    while (exists) {
        exists = false;
        for (auto& e : scene.All())
            if (e.name == name) { exists = true; break; }
        if (exists) name = base + " (" + std::to_string(suffix++) + ")";
    }
    return name;
}

static void PasteEntity(Scene& scene)
{
    if (g_CopiedEntity.id == 0) return;
    uint64_t newId = scene.CreateEntity(MakeUniqueName(scene, g_CopiedEntity.name));
    Entity* e = scene.FindEntity(newId);
    if (!e) return;
    *e = g_CopiedEntity;
    e->id = newId;
    e->meshDirty = true;
    g_SelectedEntity = newId;
    scene.ClearRedo();
    scene.PushUndo(UndoEntry::Removed, *e);
}

static void CopyEntity(Scene& scene)
{
    Entity* e = scene.FindEntity(g_SelectedEntity);
    if (e) g_CopiedEntity = *e;
}
#endif

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int)
{
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
    auto SetDpiCtx = reinterpret_cast<BOOL (WINAPI*)(HANDLE)>(
        GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDpiAwarenessContext"));
#pragma GCC diagnostic pop
    if (SetDpiCtx)
        SetDpiCtx(reinterpret_cast<HANDLE>(static_cast<intptr_t>(-4)));

    // ── Parse --dx command-line argument ──
    int forceDX = 0; // 0 = auto, 10 = D3D10 only, 11 = D3D11 only
    int argc;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argvW) {
        for (int i = 1; i < argc; i++) {
            if (wcsncmp(argvW[i], L"--dx=", 5) == 0) {
                int val = _wtoi(argvW[i] + 5);
                if (val == 10) forceDX = 10;
                else if (val == 11) forceDX = 11;
            }
        }
        LocalFree(argvW);
    }

    const char* windowTitle = "4xEngine";
#ifdef EDITOR_BUILD
    windowTitle = "4xEngine - Project Manager";
#endif
    if (!Window::Create(hInst, 1280, 720, windowTitle))
        return -1;

    // Pump initial messages so the window doesn't appear "not responding"
    Window::ProcessMessages();

    try {
        if (forceDX != 10) {
            if (d3d11::Device::Init(Window::Handle(), Window::Width(), Window::Height())) {
                Window::ProcessMessages();
                if (d3d11::Pipeline::Init(Window::Width(), Window::Height())) {
                    g_Backend = GfxBackend::D3D11;
                    d3d11::Skybox::Init();
                } else {
                    d3d11::Device::Shutdown();
                }
            }
        }

        if (g_Backend == GfxBackend::None && forceDX != 11) {
            Window::ProcessMessages();
            if (!d3d10::Device::Init(Window::Handle(), Window::Width(), Window::Height()))
                return -1;
            if (!d3d10::Pipeline::Init(Window::Width(), Window::Height()))
                return -1;
            g_Backend = GfxBackend::D3D10;
            d3d10::Skybox::Init();
        }
    } catch (const std::exception& e) {
        std::string msg = std::string("DirectX initialization error:\n") + e.what();
        MessageBoxA(nullptr, msg.c_str(), "Error", MB_OK);
        return -1;
    }
    if (g_Backend == GfxBackend::None) {
        MessageBoxA(nullptr, "No compatible DirectX device found", "Error", MB_OK);
        return -1;
    }

    Window::ProcessMessages();

    void* devPtr = (g_Backend == GfxBackend::D3D10)
        ? (void*)d3d10::Device::GetD3D()
        : (void*)d3d11::Device::GetD3D();
    void* ctxPtr = (g_Backend == GfxBackend::D3D10)
        ? nullptr
        : (void*)d3d11::Device::GetCtx();

#ifdef EDITOR_BUILD
    if (!Overlay::Init(Window::Handle(), devPtr, ctxPtr, g_Backend))
        return -1;
#endif

    g_Culling.Init(devPtr, ctxPtr, g_Backend == GfxBackend::D3D11);

    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *lastSlash = '\0';
    std::string dir(exeDir);
    if (dir.back() != '/' && dir.back() != '\\') dir += '/';

    // Detect game mode: look for game_data.pak or data.gaf
    std::string pakPath = dir + "game_data.pak";
    std::string gafPath = dir + "data.gaf";
    bool isGameMode = (GetFileAttributesA(pakPath.c_str()) != INVALID_FILE_ATTRIBUTES) ||
                      (GetFileAttributesA(gafPath.c_str()) != INVALID_FILE_ATTRIBUTES);

    if (isGameMode) {
        g_GameMode = true;

        std::vector<Entity> loaded;
        if (ReadGAF(gafPath.c_str(), loaded)) {
            for (auto& e : loaded)
                g_Scene.All().push_back(std::move(e));
        } else {
            GameArchive archive;
            if (archive.Mount(exeDir)) {
                if (LoadEntitiesFromArchive(archive, loaded)) {
                    for (auto& e : loaded)
                        g_Scene.All().push_back(std::move(e));
                }
            }
        }
        g_Scene.DeduplicateIds();

        // Initialize skybox time from WorldEnvironment
        Entity* we = g_Scene.FindWorldEnvironment();
        if (we) {
            if (g_Backend == GfxBackend::D3D10)
                d3d10::Skybox::SetTimeOfDay(we->worldEnv.timeOfDay);
            else
                d3d11::Skybox::SetTimeOfDay(we->worldEnv.timeOfDay);
        }

        g_ScriptEngineInst = new ScriptEngine();
        g_ScriptEngineInst->SetScene(&g_Scene);
        for (auto& e : g_Scene.All()) {
            if (e.HasFlag(ENTITY_SCRIPT) && !e.scriptPath.empty() && e.scriptHandle < 0) {
                Entity* sv = g_Scene.FindServerService();
                if (sv && e.parentId == sv->id)
                    e.scriptHandle = g_ScriptEngineInst->LoadServerScript(e.scriptPath);
                else
                    e.scriptHandle = g_ScriptEngineInst->LoadEntityScript(e.scriptPath, e.id);
            }
        }

        // Auto-activate first camera if none active
        if (!GetActiveCamera())
            for (auto& e : g_Scene.All())
                if (e.HasFlag(ENTITY_CAMERA)) { e.camera.isActive = true; break; }
        SetWindowTextA(Window::Handle(), "4xEngine - Game");
    }
#ifdef EDITOR_BUILD
    else {
        HKEY hKey;
        char recentProj[MAX_PATH] = {};
        DWORD regSize = sizeof(recentProj);
        if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\4xEngine", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            if (RegQueryValueExA(hKey, "RecentProject", nullptr, nullptr, (LPBYTE)recentProj, &regSize) == ERROR_SUCCESS) {
                if (GetFileAttributesA((std::string(recentProj) + "\\project.4pf").c_str()) != INVALID_FILE_ATTRIBUTES ||
                    GetFileAttributesA((std::string(recentProj) + "\\project.4xp").c_str()) != INVALID_FILE_ATTRIBUTES) {
                    ProjectManagerUI::g_ProjectFolder = recentProj;
                    ProjectManagerUI::g_Done = true;
                    std::string sceneFile = std::string(recentProj) + "\\scenes\\default.gaf";
                    std::vector<Entity> loaded;
                    if (ReadGAF(sceneFile.c_str(), loaded)) {
                        for (auto& e : loaded)
                            g_Scene.All().push_back(std::move(e));
                        g_Scene.DeduplicateIds();
                        printf("[4xLang] Loaded scene from project: %s\n", sceneFile.c_str());
                    }
                }
            }
            RegCloseKey(hKey);
        }

        if (g_Scene.All().empty() && ProjectManagerUI::g_ProjectFolder.empty()) {
            ProjectManagerUI::g_Done = false;
            ProjectManagerUI::g_Selected = false;
            ProjectManagerUI::g_ProjectFolder.clear();
        }

        g_ScriptEngineInst = new ScriptEngine();
        g_ScriptEngineInst->SetScene(&g_Scene);

        Entity* we = g_Scene.FindWorldEnvironment();
        if (we) {
            if (g_Backend == GfxBackend::D3D10)
                d3d10::Skybox::SetTimeOfDay(we->worldEnv.timeOfDay);
            else
                d3d11::Skybox::SetTimeOfDay(we->worldEnv.timeOfDay);
        }

        SetWindowTextA(Window::Handle(), "4xEngine");
    }
#endif

    LARGE_INTEGER freq, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    double totalTime = 0.0;

    g_Camera.SetPosition(0.0f, 2.0f, -5.0f);

    bool running = true;
    while (running) {
        running = Window::ProcessMessages();
        if (!running) break;

        if (Window::Resized()) {
            if (g_Backend == GfxBackend::D3D10) d3d10::Device::Resize(Window::Width(), Window::Height());
            else                                 d3d11::Device::Resize(Window::Width(), Window::Height());
            Window::ClearResized();
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        static double prevTime = (double)t0.QuadPart / (double)freq.QuadPart;
        double currTime = (double)now.QuadPart / (double)freq.QuadPart;
        float dt = (float)(currTime - prevTime);
        prevTime = currTime;
        if (dt > 0.05f) dt = 0.05f;
        totalTime += dt;

        g_Scene.Lock();

#ifdef EDITOR_BUILD
        Overlay::NewFrame();
        Overlay::BeginFrame();

        if (!ProjectManagerUI::g_Done) {
            if (ProjectManagerUI::ShowWindow()) {
                std::string folder = ProjectManagerUI::g_ProjectFolder;
                std::string sceneFile = folder + "\\scenes\\default.gaf";
                std::vector<Entity> loaded;
                if (ReadGAF(sceneFile.c_str(), loaded)) {
                    for (auto& e : loaded)
                        g_Scene.All().push_back(std::move(e));
                    g_Scene.DeduplicateIds();
                }
                Entity* we = g_Scene.FindWorldEnvironment();
                if (we) {
                    if (g_Backend == GfxBackend::D3D10)
                        d3d10::Skybox::SetTimeOfDay(we->worldEnv.timeOfDay);
                    else
                        d3d11::Skybox::SetTimeOfDay(we->worldEnv.timeOfDay);
                }
                if (g_Scene.All().empty()) {
                    g_SelectedEntity = CreateCubeEntity(g_Scene);
                    uint64_t lid = g_Scene.CreateEntity("Sun");
                    Entity* le = g_Scene.FindEntity(lid);
                    le->SetFlag(ENTITY_LIGHT);
                    le->light.type = LightType::Directional;
                    le->light.direction[0] = 0.3f;
                    le->light.direction[1] = -0.8f;
                    le->light.direction[2] = 0.5f;
                }
                SetWindowTextA(Window::Handle(), "4xEngine");
            }
            g_Scene.Unlock();
            Overlay::EndFrame(ctxPtr, g_Backend);
            if (g_Backend == GfxBackend::D3D10) d3d10::Device::Present();
            else                                 d3d11::Device::Present();
            continue;
        }
#endif

#ifdef EDITOR_BUILD
        if (!g_GameMode && !g_PlayMode) {
            bool deleteDown = Window::Keys()[VK_DELETE];
            if (deleteDown && !g_DeleteKeyHeld) {
                DeleteSelectedEntity();
            }
            g_DeleteKeyHeld = deleteDown;

            bool ctrl = GetAsyncKeyState(VK_CONTROL) & 0x8000;
            bool zDown = Window::Keys()['Z'];
            bool yDown = Window::Keys()['Y'];
            bool cDown = Window::Keys()['C'];
            bool vDown = Window::Keys()['V'];

            // Copy selected entity (Ctrl+C)
            if (ctrl && cDown && !g_CopyHeld && g_SelectedEntity != 0)
                CopyEntity(g_Scene);
            g_CopyHeld = ctrl && cDown;

            // Paste entity (Ctrl+V)
            if (ctrl && vDown && !g_PasteHeld) {
                if (g_CopiedEntity.id != 0) PasteEntity(g_Scene);
            }
            g_PasteHeld = ctrl && vDown;

            // Undo (Ctrl+Z) — skip if code editor is focused
            if (ctrl && zDown && !g_UndoHeld && !ImGui::GetIO().WantCaptureKeyboard) {
                if (g_Scene.CanUndo()) {
                    UndoEntry ue = g_Scene.PopUndo();
                    if (ue.type == UndoEntry::Removed) {
                        if (g_Backend == GfxBackend::D3D10)
                            d3d10::Pipeline::RemoveEntityMesh(ue.entity.id);
                        else
                            d3d11::Pipeline::RemoveEntityMesh(ue.entity.id);
                        g_Scene.RemoveEntity(ue.entity.id);
                        if (g_SelectedEntity == ue.entity.id) g_SelectedEntity = 0;
                        ue.type = UndoEntry::Added;
                        g_Scene.PushRedo(std::move(ue));
                    } else if (ue.type == UndoEntry::Added) {
                        g_Scene.InsertEntity(std::move(ue.entity));
                        g_Scene.All().back().meshDirty = true;
                        g_SelectedEntity = g_Scene.All().back().id;
                        UndoEntry redoE;
                        redoE.type = UndoEntry::Removed;
                        redoE.entity = g_Scene.All().back();
                        g_Scene.PushRedo(std::move(redoE));
                    } else { // Modified — restore old entity state
                        Entity* cur = g_Scene.FindEntity(ue.entity.id);
                        if (cur) {
                            UndoEntry redoE;
                            redoE.type = UndoEntry::Modified;
                            redoE.entity = *cur;
                            g_Scene.PushRedo(std::move(redoE));
                            *cur = std::move(ue.entity);
                            cur->meshDirty = true;
                        }
                    }
                }
            }
            if (ctrl && yDown && !g_RedoHeld) {
                if (g_Scene.CanRedo()) {
                    UndoEntry ue = g_Scene.PopRedo();
                    if (ue.type == UndoEntry::Removed) {
                        if (g_Backend == GfxBackend::D3D10)
                            d3d10::Pipeline::RemoveEntityMesh(ue.entity.id);
                        else
                            d3d11::Pipeline::RemoveEntityMesh(ue.entity.id);
                        uint64_t id = ue.entity.id;
                        g_Scene.RemoveEntity(id);
                        if (g_SelectedEntity == id) g_SelectedEntity = 0;
                        ue.type = UndoEntry::Added;
                        g_Scene.PushUndo(std::move(ue));
                    } else if (ue.type == UndoEntry::Added) {
                        g_Scene.InsertEntity(std::move(ue.entity));
                        g_Scene.All().back().meshDirty = true;
                        g_SelectedEntity = g_Scene.All().back().id;
                        UndoEntry undoE;
                        undoE.type = UndoEntry::Removed;
                        undoE.entity = g_Scene.All().back();
                        g_Scene.PushUndo(std::move(undoE));
                    } else { // Modified — restore old entity state
                        Entity* cur = g_Scene.FindEntity(ue.entity.id);
                        if (cur) {
                            UndoEntry undoE;
                            undoE.type = UndoEntry::Modified;
                            undoE.entity = *cur;
                            g_Scene.PushUndo(std::move(undoE));
                            *cur = std::move(ue.entity);
                            cur->meshDirty = true;
                        }
                    }
                }
            }
            g_UndoHeld = ctrl && zDown;
            g_RedoHeld = ctrl && yDown;

            // Auto-save timer: save every 3 seconds when scene is modified
            static double s_AutoSaveElapsed = 0.0;
            static double s_LastAutoSaveCheck = totalTime;
            double ds = totalTime - s_LastAutoSaveCheck;
            s_LastAutoSaveCheck = totalTime;
            s_AutoSaveElapsed += ds;
            if (s_AutoSaveElapsed >= 3.0) {
                s_AutoSaveElapsed = 0.0;
                Overlay::SaveSceneToProject(g_Scene);
            }
        }
#endif

#ifdef EDITOR_BUILD
        // Handle play mode transition
        if (g_PlayRequest) {
            g_PlayRequest = false;
            if (g_PlayMode) {
                // Entering play mode
                g_SavedScene.clear();
                for (auto& e : g_Scene.All())
                    g_SavedScene.push_back(e);

                ClearConsole();
                ConsolePrint(CONSOLE_INFO, "=== Play Mode Started ===");

                // Load/reload all script entities
                if (g_ScriptEngineInst) {
                    for (auto& e : g_Scene.All()) {
                        if (e.HasFlag(ENTITY_SCRIPT) && !e.scriptPath.empty()) {
                            if (e.scriptHandle >= 0) {
                                g_ScriptEngineInst->UnloadScript(e.scriptHandle);
                                e.scriptHandle = -1;
                            }
                            Entity* sv = g_Scene.FindServerService();
                            if (sv && e.parentId == sv->id)
                                e.scriptHandle = g_ScriptEngineInst->LoadServerScript(e.scriptPath);
                            else
                                e.scriptHandle = g_ScriptEngineInst->LoadEntityScript(e.scriptPath, e.id);
                            if (e.scriptHandle < 0)
                                ConsolePrint(CONSOLE_ERROR, "Failed to load script: %s", e.scriptPath.c_str());
                        }
                    }
                }
                // Auto-activate first camera if none active
                if (!GetActiveCamera())
                    for (auto& e : g_Scene.All())
                        if (e.HasFlag(ENTITY_CAMERA)) { e.camera.isActive = true; break; }
                g_SelectedEntity = 0;
            } else {
                // Exiting play mode
                ConsolePrint(CONSOLE_INFO, "=== Play Mode Stopped ===");

                // Unload all scripts
                if (g_ScriptEngineInst) {
                    auto handles = g_ScriptEngineInst->GetAllHandles();
                    for (int h : handles)
                        g_ScriptEngineInst->UnloadScript(h);
                    for (auto& e : g_Scene.All())
                        e.scriptHandle = -1;
                }

                // Restore scene
                g_Scene.All().clear();
                for (auto& e : g_SavedScene)
                    g_Scene.All().push_back(e);
                g_SavedScene.clear();
                g_Scene.DeduplicateIds();
                for (auto& e : g_Scene.All())
                    e.meshDirty = true;
                g_SelectedEntity = 0;

                // Restore skybox to match restored WorldEnvironment
                Entity* we = g_Scene.FindWorldEnvironment();
                if (we) {
                    if (g_Backend == GfxBackend::D3D10) {
                        d3d10::Skybox::SetTimeOfDay(we->worldEnv.timeOfDay);
                        d3d10::Skybox::SetSkyColor(we->worldEnv.skyColor[0],
                                                    we->worldEnv.skyColor[1],
                                                    we->worldEnv.skyColor[2]);
                    } else {
                        d3d11::Skybox::SetTimeOfDay(we->worldEnv.timeOfDay);
                        d3d11::Skybox::SetSkyColor(we->worldEnv.skyColor[0],
                                                    we->worldEnv.skyColor[1],
                                                    we->worldEnv.skyColor[2]);
                    }
                }
            }
        }
#endif

        // Run game systems in game mode or play mode
        if (g_GameMode || g_PlayMode) {
            for (auto& e : g_Scene.All())
                if (e.spinning.enabled)
                    e.transform.rotation.y += e.spinning.speed * dt;
            g_4xPhys.Tick(dt, g_Scene.All());
            if (g_ScriptEngineInst) {
                g_ScriptEngineInst->Tick(dt);
#ifdef EDITOR_BUILD
                g_ScriptEngineInst->PollFileChanges();
#endif
            }
        }

        HandleCameraInput(dt);

        float aspect = (Window::Height() > 0) ? (float)Window::Width() / (float)Window::Height() : 1.f;
        XMMATRIX view, proj;
        Entity* activeCam = GetActiveCamera();
        if (activeCam) {
            view = GetCameraViewMatrix(*activeCam);
            proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(activeCam->camera.fov), aspect,
                                            activeCam->camera.nearPlane, activeCam->camera.farPlane);
        } else {
            proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.f), aspect, 0.1f, 100.f);
            view = g_Camera.GetViewMatrix();
        }

        // Compute rendering camera position (for cloud height check and cloud data)
        XMFLOAT3 cameraRenderPos;
        if (activeCam) {
            XMVECTOR camPos = XMLoadFloat3(&activeCam->transform.position);
            XMStoreFloat3(&cameraRenderPos, camPos);
        } else {
            cameraRenderPos = g_Camera.GetPosition();
        }


        d3d10::LightData ld10 = {};
        d3d11::LightData ld11 = {};

        Entity* worldEnv = g_Scene.FindWorldEnvironment();
        if (worldEnv) {
            if (g_Backend == GfxBackend::D3D10) {
                d3d10::Skybox::SetEnabled(true);
                d3d10::Skybox::SetSkyColor(worldEnv->worldEnv.skyColor[0],
                                            worldEnv->worldEnv.skyColor[1],
                                            worldEnv->worldEnv.skyColor[2]);
                auto ts = d3d10::Skybox::GetTimeState();
                float wIntensity = worldEnv->worldEnv.lightIntensity;
                ld10.sunDir[0] = -ts.sunDir[0];
                ld10.sunDir[1] = -ts.sunDir[1];
                ld10.sunDir[2] = -ts.sunDir[2];
                ld10.sunColor[0] = ts.sunColor[0] * wIntensity;
                ld10.sunColor[1] = ts.sunColor[1] * wIntensity;
                ld10.sunColor[2] = ts.sunColor[2] * wIntensity;
                ld10.sunColor[3] = 1.0f;
                ld10.flags[0] = 1.0f;
            } else {
                d3d11::Skybox::SetEnabled(true);
                d3d11::Skybox::SetSkyColor(worldEnv->worldEnv.skyColor[0],
                                             worldEnv->worldEnv.skyColor[1],
                                             worldEnv->worldEnv.skyColor[2]);
                auto ts = d3d11::Skybox::GetTimeState();
                float wIntensity = worldEnv->worldEnv.lightIntensity;
                ld11.sunDir[0] = -ts.sunDir[0];
                ld11.sunDir[1] = -ts.sunDir[1];
                ld11.sunDir[2] = -ts.sunDir[2];
                ld11.sunColor[0] = ts.sunColor[0] * wIntensity;
                ld11.sunColor[1] = ts.sunColor[1] * wIntensity;
                ld11.sunColor[2] = ts.sunColor[2] * wIntensity;
                ld11.sunColor[3] = 1.0f;
                ld11.flags[0] = 1.0f;
            }
        }

        // Entity lights: Directional entities override/author the sun; Point
        // entities accumulate into the single point-light slot. Point lights
        // no longer clobber the sun's direction/color.
        bool sunFromEntity = false;
        float pointAccumR = 0.0f, pointAccumG = 0.0f, pointAccumB = 0.0f;
        bool anyPoint = false;
        float pointPosX = 0.0f, pointPosY = 0.0f, pointPosZ = 0.0f, pointRange = 10.0f;

        for (auto& ent : g_Scene.All()) {
            if (!ent.HasFlag(ENTITY_LIGHT)) continue;
            if (ent.light.type == LightType::Directional) {
                float len = sqrtf(ent.light.direction[0]*ent.light.direction[0] +
                                  ent.light.direction[1]*ent.light.direction[1] +
                                  ent.light.direction[2]*ent.light.direction[2]);
                if (len > 1e-10f) {
                    if (!worldEnv) {
                        // No skybox sun: first directional entity becomes the sun.
                        if (!sunFromEntity) {
                            ld10.sunDir[0] = ld11.sunDir[0] = ent.light.direction[0] / len;
                            ld10.sunDir[1] = ld11.sunDir[1] = ent.light.direction[1] / len;
                            ld10.sunDir[2] = ld11.sunDir[2] = ent.light.direction[2] / len;
                            ld10.sunColor[0] = ld11.sunColor[0] = 0.0f;
                            ld10.sunColor[1] = ld11.sunColor[1] = 0.0f;
                            ld10.sunColor[2] = ld11.sunColor[2] = 0.0f;
                            ld10.flags[0] = ld11.flags[0] = 1.0f;
                            sunFromEntity = true;
                        }
                        float addR = ent.light.color[0] * ent.light.intensity;
                        float addG = ent.light.color[1] * ent.light.intensity;
                        float addB = ent.light.color[2] * ent.light.intensity;
                        ld10.sunColor[0] = ld11.sunColor[0] = fminf(ld10.sunColor[0] + addR, 3.0f);
                        ld10.sunColor[1] = ld11.sunColor[1] = fminf(ld10.sunColor[1] + addG, 3.0f);
                        ld10.sunColor[2] = ld11.sunColor[2] = fminf(ld10.sunColor[2] + addB, 3.0f);
                        ld10.sunColor[3] = ld11.sunColor[3] = 1.0f;
                    }
                    // If a WorldEnvironment sun is active, extra Directional entities
                    // are currently ignored for direction (sun stays skybox-driven)
                    // but still not allowed to corrupt the point-light slot.
                }
            } else {
                // Point light: accumulate position (last one wins, matching prior
                // behavior) and additively accumulate color, but never touch sunDir.
                XMMATRIX m = ComputeWorldMatrix(ent.transform);
                pointPosX = XMVectorGetX(m.r[3]);
                pointPosY = XMVectorGetY(m.r[3]);
                pointPosZ = XMVectorGetZ(m.r[3]);
                pointRange = ent.light.range;
                anyPoint = true;

                float addR = ent.light.color[0] * ent.light.intensity;
                float addG = ent.light.color[1] * ent.light.intensity;
                float addB = ent.light.color[2] * ent.light.intensity;
                pointAccumR = fminf(pointAccumR + addR, 3.0f);
                pointAccumG = fminf(pointAccumG + addG, 3.0f);
                pointAccumB = fminf(pointAccumB + addB, 3.0f);
            }
        }

        if (anyPoint) {
            ld10.pointPos[0] = ld11.pointPos[0] = pointPosX;
            ld10.pointPos[1] = ld11.pointPos[1] = pointPosY;
            ld10.pointPos[2] = ld11.pointPos[2] = pointPosZ;
            ld10.pointParam[0] = ld11.pointParam[0] = pointRange;
            ld10.pointColor[0] = ld11.pointColor[0] = pointAccumR;
            ld10.pointColor[1] = ld11.pointColor[1] = pointAccumG;
            ld10.pointColor[2] = ld11.pointColor[2] = pointAccumB;
            ld10.pointColor[3] = ld11.pointColor[3] = 1.0f;
            ld10.flags[1] = ld11.flags[1] = 1.0f; // point enabled
        }

        ld10.ambient[0] = ld11.ambient[0] = 0.12f;
        ld10.ambient[1] = ld11.ambient[1] = 0.12f;
        ld10.ambient[2] = ld11.ambient[2] = 0.12f;
        {
            XMFLOAT3 cp = g_Camera.GetPosition();
            ld10.cameraPos[0] = ld11.cameraPos[0] = cp.x;
            ld10.cameraPos[1] = ld11.cameraPos[1] = cp.y;
            ld10.cameraPos[2] = ld11.cameraPos[2] = cp.z;
        }

        XMMATRIX viewProj = view * proj;
        g_Culling.BeginFrame(viewProj);

        std::vector<uint64_t> visibleIds;
        std::vector<uint64_t> allIds;
        std::vector<XMMATRIX> allWorlds;
        allIds.reserve(g_Scene.All().size());
        allWorlds.reserve(g_Scene.All().size());

        for (auto& entity : g_Scene.All())
        {
            if (entity.vertices.empty()) continue;
            if (entity.meshDirty)
            {
                g_Culling.UpdateEntityAABB(entity.id, entity.vertices);
                entity.meshDirty = false;
            }
            allIds.push_back(entity.id);
            allWorlds.push_back(ComputeWorldMatrix(entity.transform));
        }

        g_Culling.CullEntities(allIds, allWorlds, visibleIds);
        g_Culling.RunOcclusionQueries(visibleIds, allWorlds, allIds);

        std::unordered_map<uint64_t, XMMATRIX> worldById;
        worldById.reserve(allIds.size());
        for (size_t i = 0; i < allIds.size(); i++)
            worldById[allIds[i]] = allWorlds[i];

        std::vector<bool> visibleSet(g_Scene.All().size(), false);
        {
            std::unordered_set<uint64_t> visSet(visibleIds.begin(), visibleIds.end());
            for (size_t i = 0; i < g_Scene.All().size(); ++i)
                if (visSet.count(g_Scene.All()[i].id))
                    visibleSet[i] = true;
        }

        // ── Batching: group visible entities by mesh identity (hash-based, O(N)) ──
        static auto MeshHash = [](const std::vector<float>& verts, const std::vector<uint32_t>& idxs) -> size_t {
            size_t h = 14695981039346656037ULL;
            auto mix = [&](const void* data, size_t len) {
                const uint8_t* p = (const uint8_t*)data;
                for (size_t i = 0; i < len; i++) {
                    h ^= (size_t)p[i];
                    h *= 1099511628211ULL;
                }
            };
            size_t vs = verts.size(), is = idxs.size();
            mix(&is, sizeof(is));
            mix(idxs.data(), is * sizeof(uint32_t));
            mix(&vs, sizeof(vs));
            mix(verts.data(), vs * sizeof(float));
            return h;
        };

        struct MeshGroup {
            Entity* first;
            std::vector<size_t> sceneIndices;
        };
        std::vector<MeshGroup> meshGroups;
        std::unordered_map<size_t, std::vector<size_t>> hashToIndices;

        for (size_t i = 0; i < g_Scene.All().size(); ++i) {
            if (!visibleSet[i]) continue;
            auto& e = g_Scene.All()[i];
            if (e.vertices.empty()) continue;
            size_t h = MeshHash(e.vertices, e.indices);
            hashToIndices[h].push_back(i);
        }

        for (auto& kv : hashToIndices) {
            auto& indices = kv.second;
            if (indices.empty()) continue;
            MeshGroup mg;
            mg.first = &g_Scene.All()[indices[0]];
            mg.sceneIndices = std::move(indices);
            // Verify first entity matches rest (hash collision safety)
            auto& refVerts = mg.first->vertices;
            auto& refIdxs  = mg.first->indices;
            for (size_t k = 1; k < mg.sceneIndices.size(); ) {
                auto& oe = g_Scene.All()[mg.sceneIndices[k]];
                if (oe.vertices.size() == refVerts.size() &&
                    oe.indices.size() == refIdxs.size() &&
                    memcmp(oe.vertices.data(), refVerts.data(), refVerts.size() * sizeof(float)) == 0 &&
                    memcmp(oe.indices.data(), refIdxs.data(), refIdxs.size() * sizeof(uint32_t)) == 0) {
                    k++;
                } else {
                    // Hash collision: treat as singleton
                    MeshGroup singleton;
                    singleton.first = &oe;
                    singleton.sceneIndices.push_back(mg.sceneIndices[k]);
                    meshGroups.push_back(std::move(singleton));
                    mg.sceneIndices.erase(mg.sceneIndices.begin() + k);
                }
            }
            if (!mg.sceneIndices.empty())
                meshGroups.push_back(std::move(mg));
        }

        if (g_Backend == GfxBackend::D3D10) {
            d3d10::Device::Clear(0.08f, 0.08f, 0.12f, 1.0f);
            if (worldEnv) {
                if (g_GameMode || g_PlayMode)
                    d3d10::Skybox::Update(dt);
                d3d10::Skybox::Draw(reinterpret_cast<const float*>(&view),
                                    reinterpret_cast<const float*>(&proj), aspect,
                                    static_cast<float>(totalTime));
            }
            d3d10::Pipeline::Bind();
            d3d10::Pipeline::SetViewProj(view, proj);
            d3d10::Pipeline::SetLightData(ld10);
            for (auto& mg : meshGroups) {
                Entity* first = mg.first;
                std::vector<XMMATRIX> batchWorlds;
                std::vector<size_t> singles;
                for (size_t si : mg.sceneIndices) {
                    auto& e = g_Scene.All()[si];
                    XMMATRIX w = ComputeWorldMatrix(e.transform);
                    if (e.instancingEnabled && mg.sceneIndices.size() >= 2)
                        batchWorlds.push_back(w);
                    else
                        singles.push_back(si);
                }
                if (batchWorlds.size() >= 2)
                    d3d10::Pipeline::DrawEntityInstanced(first->id, batchWorlds.data(), (int)batchWorlds.size(),
                        first->vertices.data(), (int)first->vertices.size() / VERTEX_STRIDE,
                        first->indices.data(), (int)first->indices.size(), false);
                else if (batchWorlds.size() == 1)
                    d3d10::Pipeline::DrawEntity(first->id, batchWorlds[0],
                        first->vertices.data(), (int)first->vertices.size() / VERTEX_STRIDE,
                        first->indices.data(), (int)first->indices.size(), false);
                for (size_t si : singles) {
                    auto& e = g_Scene.All()[si];
                    auto wit = worldById.find(e.id);
                    if (wit == worldById.end()) continue;
                    d3d10::Pipeline::DrawEntity(e.id, wit->second,
                        e.vertices.data(), (int)e.vertices.size() / VERTEX_STRIDE,
                        e.indices.data(), (int)e.indices.size(), false);
                }
            }
        } else {
            d3d11::Device::Clear(0.08f, 0.08f, 0.12f, 1.0f);
            if (worldEnv) {
                if (g_GameMode || g_PlayMode)
                    d3d11::Skybox::Update(dt);
                d3d11::Skybox::Draw(reinterpret_cast<const float*>(&view),
                                    reinterpret_cast<const float*>(&proj), aspect,
                                    static_cast<float>(totalTime));
            }
            d3d11::Pipeline::Bind();
            d3d11::Pipeline::SetViewProj(view, proj);
            d3d11::Pipeline::SetLightData(ld11);
            for (auto& mg : meshGroups) {
                Entity* first = mg.first;
                std::vector<XMMATRIX> batchWorlds;
                std::vector<size_t> singles;
                for (size_t si : mg.sceneIndices) {
                    auto& e = g_Scene.All()[si];
                    XMMATRIX w = ComputeWorldMatrix(e.transform);
                    if (e.instancingEnabled && mg.sceneIndices.size() >= 2)
                        batchWorlds.push_back(w);
                    else
                        singles.push_back(si);
                }
                if (batchWorlds.size() >= 2)
                    d3d11::Pipeline::DrawEntityInstanced(first->id, batchWorlds.data(), (int)batchWorlds.size(),
                        first->vertices.data(), (int)first->vertices.size() / VERTEX_STRIDE,
                        first->indices.data(), (int)first->indices.size(), false);
                else if (batchWorlds.size() == 1)
                    d3d11::Pipeline::DrawEntity(first->id, batchWorlds[0],
                        first->vertices.data(), (int)first->vertices.size() / VERTEX_STRIDE,
                        first->indices.data(), (int)first->indices.size(), false);
                for (size_t si : singles) {
                    auto& e = g_Scene.All()[si];
                    auto wit = worldById.find(e.id);
                    if (wit == worldById.end()) continue;
                    d3d11::Pipeline::DrawEntity(e.id, wit->second,
                        e.vertices.data(), (int)e.vertices.size() / VERTEX_STRIDE,
                        e.indices.data(), (int)e.indices.size(), false);
                }
            }
        }

        if (g_GameMode) {
            g_Scene.Unlock();
            if (g_Backend == GfxBackend::D3D10) d3d10::Device::Present();
            else                                 d3d11::Device::Present();
            continue;
        }

#ifdef EDITOR_BUILD
        Overlay::DrawMenuBar(g_Scene);
        Overlay::BeginDockspace();

        Entity* selected = nullptr;
        if (g_SelectedEntity != 0)
            selected = g_Scene.FindEntity(g_SelectedEntity);

        Overlay::ShowPerformanceWindow(g_Backend, g_Culling.GetTotalCount(), g_Culling.GetVisibleCount());

        static bool showConsole = true;
        Overlay::ShowDebugConsole(&showConsole);

        bool clickedEmpty = false;
        Overlay::ShowHierarchyWindow(g_Scene, g_SelectedEntity, clickedEmpty);

        selected = nullptr;
        if (g_SelectedEntity != 0)
            selected = g_Scene.FindEntity(g_SelectedEntity);

        Overlay::ShowPropertiesWindow(selected, g_Scene);

        if (g_SelectedEntity != 0)
            selected = g_Scene.FindEntity(g_SelectedEntity);

        if (selected && (selected->HasFlag(ENTITY_SERVER_SERVICE) || selected->HasFlag(ENTITY_SCRIPT) || selected->HasFlag(ENTITY_WORLD_ENV))) {
        } else {
            Overlay::ShowGizmo(reinterpret_cast<const float*>(&view), reinterpret_cast<const float*>(&proj), selected, g_Scene);
        }

        Overlay::DrawLightIcons(g_Scene, reinterpret_cast<const float*>(&view),
                                reinterpret_cast<const float*>(&proj),
                                Window::Width(), Window::Height());
        Overlay::DrawCameraIcons(g_Scene, reinterpret_cast<const float*>(&view),
                                 reinterpret_cast<const float*>(&proj),
                                 Window::Width(), Window::Height());

        Overlay::ShowServerServiceWindow(g_Scene);

        Overlay::ShowDirectoryManagerWindow();

        Entity* editEntity = nullptr;
        if (Overlay::g_EditingEntity != 0)
            editEntity = g_Scene.FindEntity(Overlay::g_EditingEntity);
        Overlay::ShowEditorWindow(g_Scene, editEntity);

        Overlay::EndDockspace();

        if (ImGui::IsMouseClicked(0) && !ImGui::GetIO().WantCaptureMouse && !Overlay::IsGizmoOver()) {
            ImVec2 mp = ImGui::GetMousePos();
            ImVec2 vpMin = ImGui::GetMainViewport()->Pos;
            uint64_t hit = g_Culling.PickEntity(g_Scene.All(),
                mp.x - vpMin.x, mp.y - vpMin.y,
                Window::Width(), Window::Height(), view, proj);
            Entity* hitEnt = g_Scene.FindEntity(hit);
            if (hitEnt && (hitEnt->HasFlag(ENTITY_SERVER_SERVICE) || hitEnt->HasFlag(ENTITY_WORLD_ENV) || hitEnt->HasFlag(ENTITY_LIGHT) || hitEnt->HasFlag(ENTITY_CAMERA))) {
                hit = 0;
            }
            g_SelectedEntity = hit;
        }

        Overlay::EndFrame(ctxPtr, g_Backend);
#endif

        g_Scene.Unlock();
        if (g_Backend == GfxBackend::D3D10) d3d10::Device::Present();
        else                                 d3d11::Device::Present();
    }

    g_Culling.Shutdown();
#ifdef EDITOR_BUILD
    Overlay::Shutdown();
#endif
    if (g_Backend == GfxBackend::D3D10) {
        d3d10::Skybox::Shutdown();
        d3d10::Pipeline::Shutdown();
        d3d10::Device::Shutdown();
    } else {
        d3d11::Skybox::Shutdown();
        d3d11::Pipeline::Shutdown();
        d3d11::Device::Shutdown();
    }
    Window::Shutdown();

    return 0;
}