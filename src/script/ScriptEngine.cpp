#include "ScriptEngine.h"
#include "ecs/ECS.h"
#include "core/Window.h"
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>
#include <DirectXMath.h>

using namespace DirectX;

ScriptEngine* g_ScriptEngine = nullptr;

// Input state for InputService
static int g_MouseDX = 0, g_MouseDY = 0;
static int g_LastMX = 0, g_LastMY = 0;

static uint64_t GetFileTime(const std::string& path) {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &info)) {
        ULARGE_INTEGER ui;
        ui.LowPart = info.ftLastWriteTime.dwLowDateTime;
        ui.HighPart = info.ftLastWriteTime.dwHighDateTime;
        return ui.QuadPart;
    }
    return 0;
}

// ── Infinite Loop Protection ──
static LARGE_INTEGER g_HookStartTime;
static LARGE_INTEGER g_HookFreq;
static bool g_HookTriggered = false;

static void InfiniteLoopHook(lua_State* L, lua_Debug* ar) {
    if (g_HookTriggered) return;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    double elapsed = (double)(now.QuadPart - g_HookStartTime.QuadPart) / (double)g_HookFreq.QuadPart;
    if (elapsed > 30.0) {
        g_HookTriggered = true;
        luaL_error(L, "Potential infinite loop detected (>30s)! Script aborted.");
    }
}

// ── Debug Console ──
std::vector<ConsoleEntry> g_ConsoleLog;

void ConsolePrint(int type, const char* format, ...) {
    char buf[4096];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);
    ConsoleEntry entry;
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    entry.timestamp = (double)now.QuadPart / (double)freq.QuadPart;
    entry.type = type;
    entry.text = buf;
    g_ConsoleLog.push_back(entry);
    if (g_ConsoleLog.size() > 1000)
        g_ConsoleLog.erase(g_ConsoleLog.begin());
}

void ClearConsole() {
    g_ConsoleLog.clear();
}

// --- SyncService v0.1 ---
static int lua_ss_sleep(lua_State* L) {
    double ms = luaL_checknumber(L, 1);
    if (ms > 0) {
        Sleep((DWORD)ms);
    }
    return 0;
}

static int lua_ss_getTime(lua_State* L) {
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    lua_pushnumber(L, (double)now.QuadPart / (double)freq.QuadPart);
    return 1;
}

static int lua_ss_getDelta(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xDelta");
    if (lua_isnumber(L, -1)) return 1;
    lua_pop(L, 1);
    lua_pushnumber(L, 0.0);
    return 1;
}

static int lua_ss_getDiagnostics(lua_State* L) {
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    lua_createtable(L, 0, 6);
    lua_pushstring(L, "4xLang v0.1"); lua_setfield(L, -2, "engine");
    lua_pushstring(L, "Direct3D 11"); lua_setfield(L, -2, "renderer");
    lua_pushinteger(L, sc ? (lua_Integer)sc->All().size() : 0); lua_setfield(L, -2, "entityCount");
    lua_pushinteger(L, g_ScriptEngine ? (lua_Integer)g_ScriptEngine->GetAllHandles().size() : 0); lua_setfield(L, -2, "scriptCount");
    lua_pushinteger(L, (lua_Integer)g_ConsoleLog.size()); lua_setfield(L, -2, "logCount");
    lua_pushnumber(L, 0.0); lua_setfield(L, -2, "fps");
    return 1;
}

// --- InputService v0.1 ---
static int lua_is_key(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    if (!name || !name[0]) { lua_pushboolean(L, 0); return 1; }
    int vk = 0;
    if (!name[1]) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') vk = c - 32;
        else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z')) vk = c;
        else { lua_pushboolean(L, 0); return 1; }
    } else {
        struct { const char* n; int v; } map[] = {
            {"space", VK_SPACE}, {"shift", VK_SHIFT}, {"ctrl", VK_CONTROL},
            {"alt", VK_MENU}, {"escape", VK_ESCAPE}, {"enter", VK_RETURN},
            {"tab", VK_TAB}, {"up", VK_UP}, {"down", VK_DOWN},
            {"left", VK_LEFT}, {"right", VK_RIGHT}, {"backspace", VK_BACK},
            {nullptr, 0}
        };
        for (int i = 0; map[i].n; i++) {
            if (_stricmp(name, map[i].n) == 0) { vk = map[i].v; break; }
        }
        if (!vk) { lua_pushboolean(L, 0); return 1; }
    }
    bool* keys = Window::Keys();
    lua_pushboolean(L, keys ? (keys[vk] ? 1 : 0) : 0);
    return 1;
}

static int lua_is_mouseX(lua_State* L) {
    lua_pushinteger(L, Window::MouseX());
    return 1;
}

static int lua_is_mouseY(lua_State* L) {
    lua_pushinteger(L, Window::MouseY());
    return 1;
}

static int lua_is_mouseButton(lua_State* L) {
    int btn = (int)luaL_checkinteger(L, 1);
    lua_pushboolean(L, Window::MouseDown(btn) ? 1 : 0);
    return 1;
}

static int lua_is_mouseDelta(lua_State* L) {
    lua_pushinteger(L, g_MouseDX);
    lua_pushinteger(L, g_MouseDY);
    return 2;
}

// --- EntityService v0.1 ---
static int lua_es_findByName(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) { lua_pushnil(L); return 1; }
    for (auto& e : sc->All()) {
        if (e.name == name) {
            ScriptEngine::PushEntity(L, &e);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int lua_es_findById(lua_State* L) {
    uint64_t id = (uint64_t)luaL_checkinteger(L, 1);
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) { lua_pushnil(L); return 1; }
    Entity* e = sc->FindEntity(id);
    if (e) ScriptEngine::PushEntity(L, e);
    else lua_pushnil(L);
    return 1;
}

static int lua_es_getAll(lua_State* L) {
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) { lua_newtable(L); return 1; }
    lua_newtable(L);
    int idx = 1;
    for (auto& e : sc->All()) {
        ScriptEngine::PushEntity(L, &e);
        lua_rawseti(L, -2, idx++);
    }
    return 1;
}

static int lua_es_create(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) { lua_pushnil(L); return 1; }
    uint64_t id = sc->CreateEntity(name);
    Entity* e = sc->FindEntity(id);
    if (e) ScriptEngine::PushEntity(L, e);
    else lua_pushnil(L);
    return 1;
}

static int lua_es_remove(lua_State* L) {
    if (!lua_isuserdata(L, 1)) return 0;
    uint64_t* idPtr = (uint64_t*)luaL_checkudata(L, 1, "_4xEntity");
    if (!idPtr) return 0;
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) return 0;
    sc->RemoveEntity(*idPtr);
    return 0;
}

// ── Entity lookup helper (ID-based, safe against dangling pointers) ──
static Entity* CheckEntity(lua_State* L, int index) {
    uint64_t* idPtr = (uint64_t*)luaL_checkudata(L, index, "_4xEntity");
    if (!idPtr) return nullptr;
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) return nullptr;
    return sc->FindEntity(*idPtr);
}

// --- Entity object methods ---
static int lua_entity_getPos(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, e->transform.position.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, e->transform.position.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, e->transform.position.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_entity_setPos(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) return 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "x"); e->transform.position.x = (float)luaL_optnumber(L, -1, 0);
        lua_getfield(L, 2, "y"); e->transform.position.y = (float)luaL_optnumber(L, -1, 0);
        lua_getfield(L, 2, "z"); e->transform.position.z = (float)luaL_optnumber(L, -1, 0);
        lua_pop(L, 3);
    }
    return 0;
}

static int lua_entity_getRot(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, e->transform.rotation.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, e->transform.rotation.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, e->transform.rotation.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_entity_setRot(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) return 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "x"); e->transform.rotation.x = (float)luaL_optnumber(L, -1, 0);
        lua_getfield(L, 2, "y"); e->transform.rotation.y = (float)luaL_optnumber(L, -1, 0);
        lua_getfield(L, 2, "z"); e->transform.rotation.z = (float)luaL_optnumber(L, -1, 0);
        lua_pop(L, 3);
    }
    return 0;
}

static int lua_entity_getScale(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, e->transform.scale.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, e->transform.scale.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, e->transform.scale.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_entity_setScale(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) return 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "x"); e->transform.scale.x = (float)luaL_optnumber(L, -1, 1);
        lua_getfield(L, 2, "y"); e->transform.scale.y = (float)luaL_optnumber(L, -1, 1);
        lua_getfield(L, 2, "z"); e->transform.scale.z = (float)luaL_optnumber(L, -1, 1);
        lua_pop(L, 3);
    }
    return 0;
}

static int lua_entity_lookAt(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) return 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "x"); float tx = (float)luaL_optnumber(L, -1, 0); lua_pop(L, 1);
        lua_getfield(L, 2, "y"); float ty = (float)luaL_optnumber(L, -1, 0); lua_pop(L, 1);
        lua_getfield(L, 2, "z"); float tz = (float)luaL_optnumber(L, -1, 0); lua_pop(L, 1);
        XMVECTOR pos = XMLoadFloat3(&e->transform.position);
        XMVECTOR target = XMVectorSet(tx, ty, tz, 0);
        XMVECTOR dir = XMVector3Normalize(target - pos);
        float yaw = atan2f(XMVectorGetX(dir), XMVectorGetZ(dir));
        float pitch = -asinf(XMVectorGetY(dir));
        e->transform.rotation.x = pitch;
        e->transform.rotation.y = yaw;
        e->transform.rotation.z = 0;
    }
    return 0;
}

static int lua_entity_getName(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushnil(L); return 1; }
    lua_pushstring(L, e->name.c_str());
    return 1;
}

static int lua_entity_setName(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) return 0;
    e->name = luaL_checkstring(L, 2);
    return 0;
}

static int lua_entity_getId(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushnil(L); return 1; }
    lua_pushinteger(L, (lua_Integer)e->id);
    return 1;
}

static int lua_entity_isLight(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, e->HasFlag(ENTITY_LIGHT));
    return 1;
}

static int lua_entity_isCamera(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, e->HasFlag(ENTITY_CAMERA));
    return 1;
}

// --- Physics entity methods ---
static int lua_entity_getVelocity(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushnil(L); return 1; }
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, e->physics.velocity.x); lua_setfield(L, -2, "x");
    lua_pushnumber(L, e->physics.velocity.y); lua_setfield(L, -2, "y");
    lua_pushnumber(L, e->physics.velocity.z); lua_setfield(L, -2, "z");
    return 1;
}

static int lua_entity_setVelocity(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) return 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "x"); e->physics.velocity.x = (float)luaL_optnumber(L, -1, 0); lua_pop(L, 1);
        lua_getfield(L, 2, "y"); e->physics.velocity.y = (float)luaL_optnumber(L, -1, 0); lua_pop(L, 1);
        lua_getfield(L, 2, "z"); e->physics.velocity.z = (float)luaL_optnumber(L, -1, 0); lua_pop(L, 1);
    }
    return 0;
}

static int lua_entity_getMass(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, e->physics.mass);
    return 1;
}

static int lua_entity_setMass(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) return 0;
    e->physics.mass = (float)luaL_checknumber(L, 2);
    return 0;
}

static int lua_entity_getFriction(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, e->physics.friction);
    return 1;
}

static int lua_entity_setFriction(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) return 0;
    e->physics.friction = (float)luaL_checknumber(L, 2);
    return 0;
}

static int lua_entity_getRestitution(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushnil(L); return 1; }
    lua_pushnumber(L, e->physics.restitution);
    return 1;
}

static int lua_entity_setRestitution(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) return 0;
    e->physics.restitution = (float)luaL_checknumber(L, 2);
    return 0;
}

static int lua_entity_isStatic(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, e->physics.IsStatic());
    return 1;
}

// --- LightService v0.1 ---
static int lua_ls_setColor(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e || !e->HasFlag(ENTITY_LIGHT)) return 0;
    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "r"); e->light.color[0] = (float)luaL_optnumber(L, -1, 1);
        lua_getfield(L, 2, "g"); e->light.color[1] = (float)luaL_optnumber(L, -1, 1);
        lua_getfield(L, 2, "b"); e->light.color[2] = (float)luaL_optnumber(L, -1, 1);
        lua_pop(L, 3);
    }
    return 0;
}

static int lua_ls_setIntensity(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e || !e->HasFlag(ENTITY_LIGHT)) return 0;
    e->light.intensity = (float)luaL_checknumber(L, 2);
    return 0;
}

static int lua_ls_setRange(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e || !e->HasFlag(ENTITY_LIGHT)) return 0;
    e->light.range = (float)luaL_checknumber(L, 2);
    return 0;
}

static int lua_ls_createPoint(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) { lua_pushnil(L); return 1; }
    uint64_t id = sc->CreateEntity(name);
    Entity* e = sc->FindEntity(id);
    if (!e) { lua_pushnil(L); return 1; }
    e->SetFlag(ENTITY_LIGHT);
    e->light.type = LightType::Point;
    e->light.color[0] = 1.0f;
    e->light.color[1] = 1.0f;
    e->light.color[2] = 1.0f;
    e->light.intensity = 1.0f;
    e->light.range = 10.0f;
    ScriptEngine::PushEntity(L, e);
    return 1;
}

static int lua_ls_createDirectional(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) { lua_pushnil(L); return 1; }
    uint64_t id = sc->CreateEntity(name);
    Entity* e = sc->FindEntity(id);
    if (!e) { lua_pushnil(L); return 1; }
    e->SetFlag(ENTITY_LIGHT);
    e->light.type = LightType::Directional;
    e->light.color[0] = 1.0f;
    e->light.color[1] = 1.0f;
    e->light.color[2] = 1.0f;
    e->light.intensity = 1.0f;
    e->light.direction[0] = 0.3f;
    e->light.direction[1] = -0.8f;
    e->light.direction[2] = 0.5f;
    ScriptEngine::PushEntity(L, e);
    return 1;
}

// --- CameraService v0.1 ---
static int lua_cs_getActive(lua_State* L) {
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) { lua_pushnil(L); return 1; }
    for (auto& e : sc->All()) {
        if (e.HasFlag(ENTITY_CAMERA) && e.camera.isActive) {
            ScriptEngine::PushEntity(L, &e);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int lua_cs_setActive(lua_State* L) {
    Entity* e = CheckEntity(L, 1);
    if (!e || !e->HasFlag(ENTITY_CAMERA)) return 0;
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) return 0;
    for (auto& other : sc->All()) {
        if (other.HasFlag(ENTITY_CAMERA)) other.camera.isActive = false;
    }
    e->camera.isActive = true;
    return 0;
}

static int lua_cs_create(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    Scene* sc = nullptr;
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    if (lua_islightuserdata(L, -1)) sc = (Scene*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    if (!sc) { lua_pushnil(L); return 1; }
    uint64_t id = sc->CreateEntity(name);
    Entity* e = sc->FindEntity(id);
    if (!e) { lua_pushnil(L); return 1; }
    e->SetFlag(ENTITY_CAMERA);
    e->camera.isActive = false;
    e->camera.fov = 60.0f;
    e->camera.nearPlane = 0.1f;
    e->camera.farPlane = 100.0f;
    ScriptEngine::PushEntity(L, e);
    return 1;
}

// --- WorldService helpers ---
static Entity* GetWorldEnv(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, "_4xScene");
    Scene* sc = lua_islightuserdata(L, -1) ? (Scene*)lua_touserdata(L, -1) : nullptr;
    lua_pop(L, 1);
    return sc ? sc->FindWorldEnvironment() : nullptr;
}

static int lua_ws_setSkyColor(lua_State* L) {
    Entity* we = GetWorldEnv(L);
    if (we && lua_istable(L, 1)) {
        lua_getfield(L, 1, "r"); we->worldEnv.skyColor[0] = (float)luaL_optnumber(L, -1, 0.4f);
        lua_getfield(L, 1, "g"); we->worldEnv.skyColor[1] = (float)luaL_optnumber(L, -1, 0.6f);
        lua_getfield(L, 1, "b"); we->worldEnv.skyColor[2] = (float)luaL_optnumber(L, -1, 0.9f);
        lua_pop(L, 3);
    }
    return 0;
}
static int lua_ws_getSkyColor(lua_State* L) {
    Entity* we = GetWorldEnv(L);
    if (we) {
        lua_newtable(L);
        lua_pushnumber(L, we->worldEnv.skyColor[0]); lua_setfield(L, -2, "r");
        lua_pushnumber(L, we->worldEnv.skyColor[1]); lua_setfield(L, -2, "g");
        lua_pushnumber(L, we->worldEnv.skyColor[2]); lua_setfield(L, -2, "b");
    } else { lua_pushnil(L); }
    return 1;
}
static int lua_ws_setSunBrightness(lua_State* L) {
    Entity* we = GetWorldEnv(L);
    if (we) we->worldEnv.lightIntensity = (float)luaL_checknumber(L, 1);
    return 0;
}
static int lua_ws_getSunBrightness(lua_State* L) {
    Entity* we = GetWorldEnv(L);
    lua_pushnumber(L, we ? we->worldEnv.lightIntensity : 1.0);
    return 1;
}

// --- Entity userdata metatable ---
static const luaL_Reg entity_methods[] = {
    {"getPosition", lua_entity_getPos},
    {"setPosition", lua_entity_setPos},
    {"getRotation", lua_entity_getRot},
    {"setRotation", lua_entity_setRot},
    {"getScale", lua_entity_getScale},
    {"setScale", lua_entity_setScale},
    {"lookAt", lua_entity_lookAt},
    {"getName", lua_entity_getName},
    {"setName", lua_entity_setName},
    {"getId", lua_entity_getId},
    {"isLight", lua_entity_isLight},
    {"isCamera", lua_entity_isCamera},
    {"getVelocity", lua_entity_getVelocity},
    {"setVelocity", lua_entity_setVelocity},
    {"getMass", lua_entity_getMass},
    {"setMass", lua_entity_setMass},
    {"getFriction", lua_entity_getFriction},
    {"setFriction", lua_entity_setFriction},
    {"getRestitution", lua_entity_getRestitution},
    {"setRestitution", lua_entity_setRestitution},
    {"isStatic", lua_entity_isStatic},
    {nullptr, nullptr}
};

int ScriptEngine::PushEntity(lua_State* L, Entity* e) {
    uint64_t* ud = (uint64_t*)lua_newuserdata(L, sizeof(uint64_t));
    *ud = e->id;
    luaL_getmetatable(L, "_4xEntity");
    lua_setmetatable(L, -2);
    return 1;
}

// --- ScriptEngine implementation ---
ScriptEngine::ScriptEngine() {
    g_ScriptEngine = this;
}

ScriptEngine::~ScriptEngine() {
    for (auto& pair : m_Scripts) {
        if (pair.second.L) {
            lua_close(pair.second.L);
        }
    }
    m_Scripts.clear();
    g_ScriptEngine = nullptr;
}

static std::string ReadFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static int lua_panic_handler(lua_State* L) {
    const char* msg = lua_tostring(L, -1);
    if (!msg) msg = "unknown error";
    fprintf(stderr, "[4xLang] PANIC: %s\n", msg);
    return 0;
}

void ScriptEngine::RegisterAPI(lua_State* L, uint64_t entityId, bool isServer) {
    lua_pushlightuserdata(L, (void*)m_Scene);
    lua_setfield(L, LUA_REGISTRYINDEX, "_4xScene");

    lua_pushinteger(L, (lua_Integer)entityId);
    lua_setfield(L, LUA_REGISTRYINDEX, "_4xEntityId");

    lua_pushboolean(L, isServer ? 1 : 0);
    lua_setfield(L, LUA_REGISTRYINDEX, "_4xIsServer");

    luaL_newmetatable(L, "_4xEntity");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    luaL_setfuncs(L, entity_methods, 0);
    lua_pop(L, 1);

    // SyncService v0.1
    lua_newtable(L);
    lua_pushnumber(L, 0.0);
    lua_setfield(L, LUA_REGISTRYINDEX, "_4xDelta");
    lua_pushcfunction(L, lua_ss_getDelta);
    lua_setfield(L, -2, "getDelta");
    lua_pushcfunction(L, lua_ss_sleep);
    lua_setfield(L, -2, "sleep");
    lua_pushcfunction(L, lua_ss_getTime);
    lua_setfield(L, -2, "getTime");
    lua_pushcfunction(L, lua_ss_getDiagnostics);
    lua_setfield(L, -2, "getDiagnostics");
    lua_setglobal(L, "SyncService");

    // EntityService v0.1
    lua_newtable(L);
    lua_pushcfunction(L, lua_es_findByName);
    lua_setfield(L, -2, "findByName");
    lua_pushcfunction(L, lua_es_findById);
    lua_setfield(L, -2, "findById");
    lua_pushcfunction(L, lua_es_getAll);
    lua_setfield(L, -2, "getAll");
    lua_pushcfunction(L, lua_es_create);
    lua_setfield(L, -2, "create");
    lua_pushcfunction(L, lua_es_remove);
    lua_setfield(L, -2, "remove");
    lua_setglobal(L, "EntityService");

    // LightService v0.1
    lua_newtable(L);
    lua_pushcfunction(L, lua_ls_setColor);
    lua_setfield(L, -2, "setColor");
    lua_pushcfunction(L, lua_ls_setIntensity);
    lua_setfield(L, -2, "setIntensity");
    lua_pushcfunction(L, lua_ls_setRange);
    lua_setfield(L, -2, "setRange");
    lua_pushcfunction(L, lua_ls_createPoint);
    lua_setfield(L, -2, "createPoint");
    lua_pushcfunction(L, lua_ls_createDirectional);
    lua_setfield(L, -2, "createDirectional");
    lua_setglobal(L, "LightService");

    // CameraService v0.1
    lua_newtable(L);
    lua_pushcfunction(L, lua_cs_getActive);
    lua_setfield(L, -2, "getActive");
    lua_pushcfunction(L, lua_cs_setActive);
    lua_setfield(L, -2, "setActive");
    lua_pushcfunction(L, lua_cs_create);
    lua_setfield(L, -2, "create");
    lua_setglobal(L, "CameraService");

    // WorldService v0.1
    lua_newtable(L);
    lua_pushcfunction(L, lua_ws_setSkyColor);
    lua_setfield(L, -2, "setSkyColor");
    lua_pushcfunction(L, lua_ws_getSkyColor);
    lua_setfield(L, -2, "getSkyColor");
    lua_pushcfunction(L, lua_ws_setSunBrightness);
    lua_setfield(L, -2, "setSunBrightness");
    lua_pushcfunction(L, lua_ws_getSunBrightness);
    lua_setfield(L, -2, "getSunBrightness");
    lua_setglobal(L, "WorldService");

    // InputService v0.1
    lua_newtable(L);
    lua_pushcfunction(L, lua_is_key);
    lua_setfield(L, -2, "key");
    lua_pushcfunction(L, lua_is_mouseX);
    lua_setfield(L, -2, "mouseX");
    lua_pushcfunction(L, lua_is_mouseY);
    lua_setfield(L, -2, "mouseY");
    lua_pushcfunction(L, lua_is_mouseButton);
    lua_setfield(L, -2, "mouseButton");
    lua_pushcfunction(L, lua_is_mouseDelta);
    lua_setfield(L, -2, "mouseDelta");
    lua_setglobal(L, "Input");

    // Override print for 4xLang
    lua_register(L, "print", [](lua_State* L) -> int {
        int n = lua_gettop(L);
        std::string out;
        for (int i = 1; i <= n; i++) {
            const char* s = lua_tostring(L, i);
            if (s) {
                if (i > 1) out += "\t";
                out += s;
            }
        }
        printf("[4xLang] %s\n", out.c_str());
        ConsolePrint(CONSOLE_INFO, "%s", out.c_str());
        return 0;
    });

    // Register warn and error helpers
    lua_register(L, "warn", [](lua_State* L) -> int {
        int n = lua_gettop(L);
        std::string out;
        for (int i = 1; i <= n; i++) {
            const char* s = lua_tostring(L, i);
            if (s) {
                if (i > 1) out += "\t";
                out += s;
            }
        }
        printf("[4xLang] WARN: %s\n", out.c_str());
        ConsolePrint(CONSOLE_WARN, "%s", out.c_str());
        return 0;
    });

    lua_register(L, "error", [](lua_State* L) -> int {
        int n = lua_gettop(L);
        std::string out;
        for (int i = 1; i <= n; i++) {
            const char* s = lua_tostring(L, i);
            if (s) {
                if (i > 1) out += "\t";
                out += s;
            }
        }
        printf("[4xLang] ERROR: %s\n", out.c_str());
        ConsolePrint(CONSOLE_ERROR, "%s", out.c_str());
        return 0;
    });
}

int ScriptEngine::LoadScript(const std::string& path, const std::string& source, uint64_t entityId, bool isServer) {
    if (source.empty()) return -1;
    int handle = m_NextHandle++;
    ScriptInstance& inst = m_Scripts[handle];
    inst.path = path;
    inst.source = source;
    inst.L = luaL_newstate();
    if (!inst.L) {
        inst.hasError = true;
        inst.errorMsg = "Failed to create Lua state";
        return handle;
    }
    luaL_openlibs(inst.L);
    lua_atpanic(inst.L, lua_panic_handler);
    RegisterAPI(inst.L, entityId, isServer);
    int ret = luaL_loadstring(inst.L, source.c_str());
    if (ret != LUA_OK) {
        inst.hasError = true;
        inst.errorMsg = lua_tostring(inst.L, -1);
        lua_pop(inst.L, 1);
        return handle;
    }
    ret = lua_pcall(inst.L, 0, 0, 0);
    if (ret != LUA_OK) {
        inst.hasError = true;
        inst.errorMsg = lua_tostring(inst.L, -1);
        lua_pop(inst.L, 1);
    }
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    inst.lastTick = (double)now.QuadPart / (double)freq.QuadPart;
    inst.lastWriteTime = GetFileTime(path);
    m_ScriptHandles.push_back(handle);
    return handle;
}

int ScriptEngine::LoadServerScript(const std::string& path) {
    std::string source = ReadFile(path);
    if (source.empty()) {
        std::string alt = "scripts/" + path;
        source = ReadFile(alt);
    }
    if (source.empty()) {
        printf("[4xLang] Failed to load server script: %s\n", path.c_str());
        ConsolePrint(CONSOLE_ERROR, "Failed to load server script: %s", path.c_str());
        return -1;
    }
    printf("[4xLang] Loading server script: %s\n", path.c_str());
    ConsolePrint(CONSOLE_INFO, "Loading server script: %s", path.c_str());
    return LoadScript(path, source, 0, true);
}

int ScriptEngine::LoadEntityScript(const std::string& path, uint64_t entityId) {
    std::string source = ReadFile(path);
    if (source.empty()) return -1;
    return LoadScript(path, source, entityId, false);
}

void ScriptEngine::UnloadScript(int handle) {
    auto it = m_Scripts.find(handle);
    if (it == m_Scripts.end()) return;
    if (it->second.L) lua_close(it->second.L);
    m_Scripts.erase(it);
    auto hIt = std::find(m_ScriptHandles.begin(), m_ScriptHandles.end(), handle);
    if (hIt != m_ScriptHandles.end()) m_ScriptHandles.erase(hIt);
}

#ifdef EDITOR_BUILD
bool ScriptEngine::ReloadScript(int handle) {
    auto it = m_Scripts.find(handle);
    if (it == m_Scripts.end()) return false;
    ScriptInstance& inst = it->second;
    uint64_t entityId = 0;
    bool isServer = false;
    if (inst.L) {
        lua_getfield(inst.L, LUA_REGISTRYINDEX, "_4xEntityId");
        if (lua_type(inst.L, -1) == LUA_TNUMBER) entityId = (uint64_t)lua_tointeger(inst.L, -1);
        lua_pop(inst.L, 1);
        lua_getfield(inst.L, LUA_REGISTRYINDEX, "_4xIsServer");
        if (lua_isboolean(inst.L, -1)) isServer = lua_toboolean(inst.L, -1) != 0;
        lua_pop(inst.L, 1);
        lua_close(inst.L);
        inst.L = nullptr;
    }
    std::string source = ReadFile(inst.path);
    if (source.empty()) {
        inst.hasError = true;
        inst.errorMsg = "File not found on reload";
        return false;
    }
    inst.source = source;
    inst.hasError = false;
    inst.errorMsg.clear();
    inst.L = luaL_newstate();
    if (!inst.L) {
        inst.hasError = true;
        inst.errorMsg = "Failed to create Lua state on reload";
        return false;
    }
    luaL_openlibs(inst.L);
    lua_atpanic(inst.L, lua_panic_handler);
    RegisterAPI(inst.L, entityId, isServer);
    int ret = luaL_loadstring(inst.L, source.c_str());
    if (ret != LUA_OK) {
        inst.hasError = true;
        inst.errorMsg = lua_tostring(inst.L, -1);
        lua_pop(inst.L, 1);
        return false;
    }
    ret = lua_pcall(inst.L, 0, 0, 0);
    if (ret != LUA_OK) {
        inst.hasError = true;
        inst.errorMsg = lua_tostring(inst.L, -1);
        lua_pop(inst.L, 1);
        return false;
    }
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    inst.lastTick = (double)now.QuadPart / (double)freq.QuadPart;
    inst.lastWriteTime = GetFileTime(inst.path);
    printf("[4xLang] Reloaded script: %s\n", inst.path.c_str());
    ConsolePrint(CONSOLE_INFO, "Reloaded script: %s", inst.path.c_str());
    return true;
}

void ScriptEngine::PollFileChanges() {
    for (auto& pair : m_Scripts) {
        ScriptInstance& inst = pair.second;
        if (!inst.L || inst.path.empty()) continue;
        uint64_t t = GetFileTime(inst.path);
        if (t != 0 && t != inst.lastWriteTime) {
            ConsolePrint(CONSOLE_INFO, "Hot reload: %s", inst.path.c_str());
            ReloadScript(pair.first);
        }
    }
}
#endif // EDITOR_BUILD

ScriptInstance* ScriptEngine::GetScript(int handle) {
    auto it = m_Scripts.find(handle);
    if (it == m_Scripts.end()) return nullptr;
    return &it->second;
}

int ScriptEngine::FindScriptByPath(const std::string& path) {
    for (auto& pair : m_Scripts) {
        if (pair.second.path == path) return pair.first;
    }
    return -1;
}

void ScriptEngine::Tick(float dt) {
    if (!m_Scene) return;
    LARGE_INTEGER freq, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    double currentTime = (double)now.QuadPart / (double)freq.QuadPart;

    if (Window::Handle()) {
        int mx = Window::MouseX();
        int my = Window::MouseY();
        g_MouseDX = mx - g_LastMX;
        g_MouseDY = my - g_LastMY;
        g_LastMX = mx;
        g_LastMY = my;
    } else {
        g_MouseDX = 0;
        g_MouseDY = 0;
    }

    for (auto& pair : m_Scripts) {
        ScriptInstance& inst = pair.second;
        if (!inst.L || inst.hasError) continue;
        lua_pushnumber(inst.L, dt);
        lua_setfield(inst.L, LUA_REGISTRYINDEX, "_4xDelta");
        lua_getglobal(inst.L, "update");
        if (lua_isfunction(inst.L, -1)) {
            lua_pushnumber(inst.L, dt);
            g_HookFreq = freq;
            g_HookStartTime = now;
            g_HookTriggered = false;
            lua_sethook(inst.L, InfiniteLoopHook, LUA_MASKCOUNT, 10000);
            int ret = lua_pcall(inst.L, 1, 0, 0);
            lua_sethook(inst.L, nullptr, 0, 0);
            if (ret != LUA_OK) {
                inst.hasError = true;
                const char* err = lua_tostring(inst.L, -1);
                luaL_traceback(inst.L, inst.L, err, 1);
                inst.errorMsg = lua_tostring(inst.L, -1);
                lua_pop(inst.L, 1);
                printf("[4xLang] Script error in '%s':\n%s\n", inst.path.c_str(), inst.errorMsg.c_str());
                ConsolePrint(CONSOLE_ERROR, "[%s] %s", inst.path.c_str(), inst.errorMsg.c_str());
            }
        } else {
            lua_pop(inst.L, 1);
        }
        inst.lastTick = currentTime;
    }
}
