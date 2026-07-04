#pragma once
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <windows.h>
#include "phy/Physics.h"

static constexpr int VERTEX_STRIDE = 9; // pos(3) + normal(3) + color(3) per vertex

enum EntityFlag : uint32_t {
    ENTITY_LIGHT         = 1 << 0,
    ENTITY_CAMERA        = 1 << 1,
    ENTITY_WORLD_ENV     = 1 << 2,
    ENTITY_SCRIPT        = 1 << 3,
    ENTITY_SERVER_SERVICE = 1 << 4,
};

struct TransformComponent {
    DirectX::XMFLOAT3 position = { 0, 0, 0 };
    DirectX::XMFLOAT3 scale    = { 1, 1, 1 };
    DirectX::XMFLOAT3 rotation = { 0, 0, 0 };
};

struct SpinningComponent {
    bool enabled = false;
    float speed  = 1.0f;
};

struct FaceColorsComponent {
    float colors[6][3] = {
        {0.2f, 0.4f, 1.0f},
        {1.0f, 0.2f, 0.2f},
        {0.2f, 1.0f, 0.3f},
        {1.0f, 0.9f, 0.1f},
        {0.1f, 0.9f, 1.0f},
        {1.0f, 0.2f, 0.9f},
    };
};

enum class LightType { Directional, Point };

struct LightComponent {
    LightType type = LightType::Directional;
    float color[3] = { 1.0f, 1.0f, 1.0f };
    float intensity = 1.0f;
    float direction[3] = { 0.3f, -0.8f, 0.5f };
    float range = 10.0f;
};

struct CameraComponent {
    bool isActive = false;
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

struct WorldEnvironmentComponent {
    float timeOfDay = 12.0f;        // 0-24 hours (default noon)
    float skyColor[3] = { 0.4f, 0.6f, 0.9f };
    float lightIntensity = 1.0f;    // 0-1, maps to 0-100% brightness
    bool shadowsEnabled = false;
    bool volumetricLighting = false;
};

struct Entity {
    uint64_t id;
    std::string name;
    TransformComponent transform;
    SpinningComponent spinning;
    phy::PhysicsComponent physics;
    FaceColorsComponent faceColors;
    LightComponent light;
    CameraComponent camera;
    WorldEnvironmentComponent worldEnv;
    std::vector<float> vertices;
    std::vector<uint32_t> indices;
    bool meshDirty = true;
    uint32_t flags = 0;
    uint64_t parentId = 0;           // 0 = root, non-zero = child of parent
    std::string scriptPath;          // .4xs file path for script entities
    int scriptHandle = -1;           // handle into ScriptEngine (-1 = not loaded)

    Entity(uint64_t id, const std::string& name) : id(id), name(name) {}

    bool HasFlag(EntityFlag f) const { return (flags & (uint32_t)f) != 0; }
    void SetFlag(EntityFlag f) { flags |= (uint32_t)f; }
    void ClearFlag(EntityFlag f) { flags &= ~(uint32_t)f; }
};

void BuildCubeMesh(const float faceColors[6][3], std::vector<float>& verts, std::vector<uint32_t>& indices);

class Scene;
uint64_t CreateCubeEntity(Scene& scene);
uint64_t CreateSphereEntity(Scene& scene);
uint64_t CreateCapsuleEntity(Scene& scene);
uint64_t CreatePlaneEntity(Scene& scene);
uint64_t CreateTriangleEntity(Scene& scene);
uint64_t CreateOctagonEntity(Scene& scene);
uint64_t CreateSnowmanEntity(Scene& scene);

// --- GAF archive (scene serialization) ---
#pragma pack(push, 1)
struct GAFHeader {
    char magic[4] = { 'G', 'A', 'F', '2' };
    uint32_t version = 1;
    uint32_t entityCount = 0;
    uint64_t dataSize = 0;
};
#pragma pack(pop)

bool WriteGAF(const char* path, const std::vector<Entity>& entities);
bool ReadGAF(const char* path, std::vector<Entity>& entities);

class GameArchive;
bool LoadEntitiesFromArchive(GameArchive& archive, std::vector<Entity>& entities);

#ifdef EDITOR_BUILD
bool ExportArchive(const char* outputDir, const std::vector<Entity>& entities);

struct UndoEntry {
    enum Type { Added, Removed };
    Type type;
    Entity entity;
    UndoEntry() : type(Added), entity(0, "") {}
};
#endif

class Scene {
public:
    Scene();
    ~Scene();

    uint64_t CreateEntity(const std::string& name);
    bool RemoveEntity(uint64_t id);
    void InsertEntity(Entity e);
    Entity* FindEntity(uint64_t id);
    std::vector<Entity>& All() { return m_Entities; }

    void Lock()   { EnterCriticalSection(&m_Lock); }
    void Unlock() { LeaveCriticalSection(&m_Lock); }

    // Hierarchy helpers
#ifdef EDITOR_BUILD
    void ReparentEntity(uint64_t entityId, uint64_t newParentId);
    std::vector<Entity*> GetChildren(uint64_t parentId);
    bool IsChildOf(uint64_t entityId, uint64_t potentialParent);
#endif

    // Find ServerService entity
    Entity* FindServerService();

    // Find WorldEnvironment entity
    Entity* FindWorldEnvironment();

    // Fix duplicate IDs and recalculate m_NextId (call after loading entities)
    void DeduplicateIds() {
        std::vector<uint64_t> remap;
        for (size_t i = 0; i < m_Entities.size(); i++) {
            bool dup = false;
            for (size_t j = 0; j < i; j++) {
                if (m_Entities[j].id == m_Entities[i].id) { dup = true; break; }
            }
            if (dup) {
                uint64_t oldId = m_Entities[i].id;
                uint64_t newId = m_NextId++;
                m_Entities[i].id = newId;
                remap.push_back(oldId);
                remap.push_back(newId);
            }
        }
        if (!remap.empty()) {
            for (auto& e : m_Entities) {
                for (size_t r = 0; r < remap.size(); r += 2) {
                    if (e.parentId == remap[r]) {
                        e.parentId = remap[r + 1];
                    }
                }
            }
        }
        for (auto& e : m_Entities)
            if (e.id >= m_NextId) m_NextId = e.id + 1;
    }

    // Modified tracking for auto-save
    void MarkModified() { m_Modified = true; }
    bool IsModified() const { return m_Modified; }
    void ClearModified() { m_Modified = false; }

#ifdef EDITOR_BUILD
    void PushUndo(UndoEntry::Type type, const Entity& e);
    void PushUndo(UndoEntry entry);
    bool CanUndo() const { return !m_UndoStack.empty(); }
    bool CanRedo() const { return !m_RedoStack.empty(); }
    UndoEntry PopUndo();
    UndoEntry PopRedo();
    void PushRedo(UndoEntry entry);
    void ClearRedo() { m_RedoStack.clear(); }
#endif

private:
    CRITICAL_SECTION m_Lock;
    uint64_t m_NextId = 1;
    std::vector<Entity> m_Entities;
    bool m_Modified = false;
#ifdef EDITOR_BUILD
    std::vector<UndoEntry> m_UndoStack;
    std::vector<UndoEntry> m_RedoStack;
    static const size_t MAX_UNDO = 50;
#endif
};

DirectX::XMMATRIX ComputeWorldMatrix(const TransformComponent& tc);