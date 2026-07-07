#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct Entity;
class Scene;

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

// 4xLang v0.1 -- Scripting engine wrapping LuaJIT
struct ScriptInstance {
    std::string path;         // .scr / .4xs file path
    std::string source;       // raw source text
    lua_State* L = nullptr;   // dedicated Lua state per script
    bool hasError = false;
    std::string errorMsg;
    double lastTick = 0.0;
    uint64_t lastWriteTime = 0;
};

class ScriptEngine {
public:
    ScriptEngine();
    ~ScriptEngine();

    void SetScene(Scene* scene) { m_Scene = scene; }

    // Load a .scr / .4xs script file as a ServerService script (global scope)
    int LoadServerScript(const std::string& path);

    // Load a .scr / .4xs script file as an entity-attached script
    int LoadEntityScript(const std::string& path, uint64_t entityId);

    // Unload a script instance
    void UnloadScript(int handle);

    // Tick all loaded scripts
    void Tick(float dt);

#ifdef EDITOR_BUILD
    // Reload a script by handle
    bool ReloadScript(int handle);

    // Poll all loaded scripts for file changes and reload if modified
    void PollFileChanges();
#endif

    // Get script instance by handle
    ScriptInstance* GetScript(int handle);

    // Find handle for a script path
    int FindScriptByPath(const std::string& path);

    // Get all script handles
    const std::vector<int>& GetAllHandles() const { return m_ScriptHandles; }

    // Access to raw Lua state for API binding
    static int PushEntity(lua_State* L, Entity* e);

private:
    Scene* m_Scene = nullptr;
    std::unordered_map<int, ScriptInstance> m_Scripts;
    std::vector<int> m_ScriptHandles;
    int m_NextHandle = 1;

    int LoadScript(const std::string& path, const std::string& source, uint64_t entityId, bool isServer);

    // Register 4xLang API functions
    void RegisterAPI(lua_State* L, uint64_t entityId, bool isServer);

    // Create a shared metatable for services
    void CreateServiceTable(lua_State* L);
};

// Global engine pointer for C API callbacks
extern ScriptEngine* g_ScriptEngine;

// ── Debug Console (4xLang v0.1) ──
struct ConsoleEntry {
    double timestamp;
    int type; // 0=info, 1=warn, 2=error
    std::string text;
};

extern std::vector<ConsoleEntry> g_ConsoleLog;
extern void ConsolePrint(int type, const char* format, ...);
extern void ClearConsole();
enum { CONSOLE_INFO = 0, CONSOLE_WARN = 1, CONSOLE_ERROR = 2 };
