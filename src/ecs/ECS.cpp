#include "ECS.h"
#include "io/Archive.h"
#include <cstring>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <unordered_map>
#include <sys/stat.h>

extern "C" {
#include <zstd.h>
}

using namespace DirectX;

struct WriteBuf {
    std::vector<uint8_t> buf;
    void write(const void* data, size_t sz) {
        const uint8_t* p = (const uint8_t*)data;
        buf.insert(buf.end(), p, p + sz);
    }
    template<typename T> void put(const T& v) { write(&v, sizeof(v)); }
    void str(const std::string& s) {
        uint32_t len = (uint32_t)s.size();
        put(len);
        write(s.data(), len);
    }
};

static void serialize_entity(WriteBuf& w, const Entity& e) {
    w.put(e.id);
    w.str(e.name);
    w.put(e.transform.position.x); w.put(e.transform.position.y); w.put(e.transform.position.z);
    w.put(e.transform.scale.x);    w.put(e.transform.scale.y);    w.put(e.transform.scale.z);
    w.put(e.transform.rotation.x); w.put(e.transform.rotation.y); w.put(e.transform.rotation.z);
    w.put((uint8_t)(e.spinning.enabled ? 1 : 0));
    w.put(e.spinning.speed);
    w.put((uint8_t)(e.physics.enabled ? 1 : 0));
    w.put(e.physics.mass);
    w.put(e.physics.restitution);
    w.put(e.physics.friction);
    w.put(e.physics.velocity.x);
    w.put(e.physics.velocity.y);
    w.put(e.physics.velocity.z);
    w.put((uint8_t)(e.physics.collidable ? 1 : 0));
    for (int f = 0; f < 6; f++)
        for (int c = 0; c < 3; c++)
            w.put(e.faceColors.colors[f][c]);
    w.put((uint8_t)(e.HasFlag(ENTITY_LIGHT) ? 1 : 0));
    w.put((uint32_t)e.light.type);
    w.put(e.light.color[0]); w.put(e.light.color[1]); w.put(e.light.color[2]);
    w.put(e.light.intensity);
    w.put(e.light.direction[0]); w.put(e.light.direction[1]); w.put(e.light.direction[2]);
    w.put(e.light.range);
    w.put((uint8_t)(e.HasFlag(ENTITY_CAMERA) ? 1 : 0));
    w.put((uint8_t)(e.camera.isActive ? 1 : 0));
    w.put(e.camera.fov);
    w.put(e.camera.nearPlane);
    w.put(e.camera.farPlane);
    uint32_t vc = (uint32_t)e.vertices.size();
    w.put(vc);
    if (vc) w.write(e.vertices.data(), vc * sizeof(float));
    uint32_t ic = (uint32_t)e.indices.size();
    w.put(ic);
    if (ic) w.write(e.indices.data(), ic * sizeof(uint32_t));

    w.put(e.parentId);
    w.put((uint8_t)(e.HasFlag(ENTITY_SCRIPT) ? 1 : 0));
    w.put((uint8_t)(e.HasFlag(ENTITY_SERVER_SERVICE) ? 1 : 0));
    w.str(e.scriptPath);

    w.put((uint8_t)(e.HasFlag(ENTITY_WORLD_ENV) ? 1 : 0));
    w.put(e.worldEnv.timeOfDay);
    w.put(e.worldEnv.skyColor[0]); w.put(e.worldEnv.skyColor[1]); w.put(e.worldEnv.skyColor[2]);
    w.put((uint8_t)(e.worldEnv.shadowsEnabled ? 1 : 0));
    w.put((uint8_t)(e.worldEnv.volumetricLighting ? 1 : 0));
    w.put(e.worldEnv.lightIntensity);

    w.put((uint8_t)(e.HasFlag(ENTITY_MODEL) ? 1 : 0));
}

struct ReadBuf {
    const uint8_t* ptr;
    size_t remain;
    ReadBuf(const uint8_t* p, size_t sz) : ptr(p), remain(sz) {}
    bool read(void* data, size_t sz) {
        if (remain < sz) return false;
        memcpy(data, ptr, sz);
        ptr += sz; remain -= sz;
        return true;
    }
    template<typename T> bool get(T& v) { return read(&v, sizeof(v)); }
    bool str(std::string& s) {
        uint32_t len;
        if (!get(len)) return false;
        if (remain < len) return false;
        s.assign((const char*)ptr, len);
        ptr += len; remain -= len;
        return true;
    }
};

static bool deserialize_entity(ReadBuf& r, Entity& e, uint32_t version = 1) {
    if (!r.get(e.id)) return false;
    if (!r.str(e.name)) return false;
    if (!r.get(e.transform.position.x)) return false;
    if (!r.get(e.transform.position.y)) return false;
    if (!r.get(e.transform.position.z)) return false;
    if (!r.get(e.transform.scale.x)) return false;
    if (!r.get(e.transform.scale.y)) return false;
    if (!r.get(e.transform.scale.z)) return false;
    if (!r.get(e.transform.rotation.x)) return false;
    if (!r.get(e.transform.rotation.y)) return false;
    if (!r.get(e.transform.rotation.z)) return false;
    uint8_t spin;
    if (!r.get(spin)) return false;
    e.spinning.enabled = (spin != 0);
    if (!r.get(e.spinning.speed)) return false;
    uint8_t phy;
    if (!r.get(phy)) return false;
    e.physics.enabled = (phy != 0);
    if (!r.get(e.physics.mass)) return false;
    if (!r.get(e.physics.restitution)) return false;
    if (!r.get(e.physics.friction)) return false;
    if (!r.get(e.physics.velocity.x)) return false;
    if (!r.get(e.physics.velocity.y)) return false;
    if (!r.get(e.physics.velocity.z)) return false;
    if (version >= 2) {
        uint8_t col;
        if (!r.get(col)) return false;
        e.physics.collidable = (col != 0);
    }
    for (int f = 0; f < 6; f++)
        for (int c = 0; c < 3; c++)
            if (!r.get(e.faceColors.colors[f][c])) return false;
    uint8_t isl;
    if (!r.get(isl)) return false;
    if (isl) e.SetFlag(ENTITY_LIGHT);
    uint32_t lt;
    if (!r.get(lt)) return false;
    e.light.type = (LightType)lt;
    if (!r.get(e.light.color[0])) return false;
    if (!r.get(e.light.color[1])) return false;
    if (!r.get(e.light.color[2])) return false;
    if (!r.get(e.light.intensity)) return false;
    if (!r.get(e.light.direction[0])) return false;
    if (!r.get(e.light.direction[1])) return false;
    if (!r.get(e.light.direction[2])) return false;
    if (!r.get(e.light.range)) return false;
    uint8_t isc;
    if (!r.get(isc)) return false;
    if (isc) e.SetFlag(ENTITY_CAMERA);
    uint8_t ca;
    if (!r.get(ca)) return false;
    e.camera.isActive = (ca != 0);
    if (!r.get(e.camera.fov)) return false;
    if (!r.get(e.camera.nearPlane)) return false;
    if (!r.get(e.camera.farPlane)) return false;
    uint32_t vc;
    if (!r.get(vc)) return false;
    e.vertices.resize(vc);
    if (vc && !r.read(e.vertices.data(), vc * sizeof(float))) return false;
    uint32_t ic;
    if (!r.get(ic)) return false;
    e.indices.resize(ic);
    if (ic && !r.read(e.indices.data(), ic * sizeof(uint32_t))) return false;

    // Optional fields (older format) - all must succeed or we reject the entity
    {
        uint64_t pid;
        if (!r.get(pid)) return false;
        e.parentId = pid;
        uint8_t isc_s, iss;
        if (!r.get(isc_s)) return false;
        if (isc_s) e.SetFlag(ENTITY_SCRIPT);
        if (!r.get(iss)) return false;
        if (iss) e.SetFlag(ENTITY_SERVER_SERVICE);
        if (!r.str(e.scriptPath)) return false;
    }

    {
        uint8_t iswe;
        if (!r.get(iswe)) return false;
        if (iswe) e.SetFlag(ENTITY_WORLD_ENV);
        if (!r.get(e.worldEnv.timeOfDay)) return false;
        if (!r.get(e.worldEnv.skyColor[0])) return false;
        if (!r.get(e.worldEnv.skyColor[1])) return false;
        if (!r.get(e.worldEnv.skyColor[2])) return false;
        uint8_t sw, vl;
        if (!r.get(sw)) return false;
        e.worldEnv.shadowsEnabled = (sw != 0);
        if (!r.get(vl)) return false;
        e.worldEnv.volumetricLighting = (vl != 0);
        if (!r.get(e.worldEnv.lightIntensity)) return false;
    }

    if (version >= 3) {
        uint8_t ismdl;
        if (!r.get(ismdl)) return false;
        if (ismdl) e.SetFlag(ENTITY_MODEL);
    }

    e.meshDirty = true;
    return true;
}

bool WriteGAF(const char* path, const std::vector<Entity>& entities) {
    WriteBuf w;
    uint32_t count = (uint32_t)entities.size();
    w.put(count);
    for (auto& e : entities)
        serialize_entity(w, e);

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    GAFHeader hdr;
    hdr.version = 3;
    hdr.entityCount = count;
    hdr.dataSize = w.buf.size();
    fwrite(&hdr, sizeof(hdr), 1, f);
    fwrite(w.buf.data(), 1, w.buf.size(), f);
    fclose(f);
    return true;
}

static bool ReadGAFHeader(FILE* f, GAFHeader& hdr, uint32_t& version) {
    char magic[4];
    if (fread(magic, 1, 4, f) != 4) return false;
    if (magic[0] != 'G' || magic[1] != 'A' || magic[2] != 'F') return false;
    if (magic[3] == '1') {
        version = 1;
        if (fread(&hdr.entityCount, sizeof(hdr.entityCount), 1, f) != 1) return false;
        if (fread(&hdr.dataSize, sizeof(hdr.dataSize), 1, f) != 1) return false;
        return true;
    }
    if (magic[3] == '2') {
        if (fread(&hdr.version, sizeof(hdr.version), 1, f) != 1) return false;
        if (fread(&hdr.entityCount, sizeof(hdr.entityCount), 1, f) != 1) return false;
        // Read dataSize candidate at offset 12. If unreasonably large (>100MB),
        // assume old 24-byte header layout (4 bytes padding between entityCount
        // and dataSize). In that case read the real dataSize from offset 16.
        if (fread(&hdr.dataSize, sizeof(hdr.dataSize), 1, f) != 1) return false;
        if (hdr.dataSize > 100ULL * 1024 * 1024) {
            // Old 24-byte header layout had 4 bytes padding between entityCount
            // and dataSize. Seek back 4 bytes (to offset 16) and re-read.
            fseek(f, -4, SEEK_CUR);
            if (fread(&hdr.dataSize, sizeof(hdr.dataSize), 1, f) != 1) return false;
        }
        version = hdr.version;
        return true;
    }
    return false;
}

bool ReadGAF(const char* path, std::vector<Entity>& entities) {
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    GAFHeader hdr;
    uint32_t version = 1;
    if (!ReadGAFHeader(f, hdr, version)) { fclose(f); return false; }

    std::vector<uint8_t> data(hdr.dataSize);
    if (fread(data.data(), 1, hdr.dataSize, f) != hdr.dataSize) {
        fclose(f); return false;
    }
    fclose(f);

    ReadBuf r(data.data(), data.size());
    uint32_t count;
    if (!r.get(count)) return false;
    entities.clear();
    entities.reserve(count);
    for (uint32_t i = 0; i < count; i++) {
        Entity e(0, "");
        if (!deserialize_entity(r, e, version)) return false;
        entities.push_back(std::move(e));
    }
    return true;
}

bool LoadEntitiesFromArchive(GameArchive& archive, std::vector<Entity>& entities) {
    entities.clear();

    const auto& index = archive.GetIndex();
    if (index.empty()) return false;

    // Group entities by block index (O(N) with unordered_map)
    struct Group { int blockIdx; std::vector<size_t> entries; };
    std::unordered_map<uint32_t, std::vector<size_t>> groupMap;
    for (size_t i = 0; i < index.size(); i++)
        groupMap[index[i].blockIndex].push_back(i);
    std::vector<Group> groups;
    groups.reserve(groupMap.size());
    for (auto& kv : groupMap) {
        Group g;
        g.blockIdx = (int)kv.first;
        g.entries = std::move(kv.second);
        groups.push_back(std::move(g));
    }

    for (auto& group : groups) {
        std::vector<uint8_t> blockData = archive.GetBlockData(group.blockIdx);
        if (blockData.empty()) return false;

        // Sort entries by offset within block for sequential reading
        std::sort(group.entries.begin(), group.entries.end(),
            [&](size_t a, size_t b) { return index[a].offsetInBlock < index[b].offsetInBlock; });

        for (size_t ei : group.entries) {
            const auto& entry = index[ei];
            if (entry.offsetInBlock + entry.size > (uint32_t)blockData.size()) return false;
            ReadBuf er(blockData.data() + entry.offsetInBlock, entry.size);
            Entity e(0, "");
            if (!deserialize_entity(er, e, 3)) return false;
            entities.push_back(std::move(e));
        }
    }

    return true;
}

#ifdef EDITOR_BUILD

static const uint64_t TARGET_BLOCK_SIZE = 8ULL * 1024 * 1024; // 8 MB per block

bool ExportArchive(const char* outputDir, const std::vector<Entity>& entities) {
    if (entities.empty()) return false;

    // Serialize each entity
    struct SerializedEntity {
        std::string name;
        std::vector<uint8_t> data;
    };
    std::vector<SerializedEntity> serialized;
    serialized.reserve(entities.size());
    for (auto& e : entities) {
        WriteBuf w;
        serialize_entity(w, e);
        serialized.push_back({ e.name, std::move(w.buf) });
    }

    // Group into blocks of ~TARGET_BLOCK_SIZE
    struct Block {
        std::vector<uint8_t> data;      // uncompressed concatenated entity data
        std::vector<uint32_t> offsets;  // offset of each entity within data
        std::vector<uint32_t> sizes;    // size of each entity within data
        std::vector<std::string> names;
    };
    std::vector<Block> blocks;
    blocks.emplace_back();
    uint64_t curSize = 0;

    for (auto& se : serialized) {
        if (curSize + se.data.size() > TARGET_BLOCK_SIZE && curSize > 0) {
            blocks.emplace_back();
            curSize = 0;
        }
        Block& bk = blocks.back();
        bk.offsets.push_back((uint32_t)bk.data.size());
        bk.sizes.push_back((uint32_t)se.data.size());
        bk.names.push_back(se.name);
        bk.data.insert(bk.data.end(), se.data.begin(), se.data.end());
        curSize += se.data.size();
    }

    // Build output path
    std::string outDir(outputDir);
    char last = outDir.empty() ? 0 : outDir.back();
    if (last != '/' && last != '\\') outDir += '/';
    std::string pakPath = outDir + "game_data.pak";

    FILE* f = fopen(pakPath.c_str(), "wb");
    if (!f) return false;

    // Reserve space for header (will write at end with real indexOffset)
    PAK2Header hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, "PAK2", 4);
    hdr.version = 2;
    hdr.entityCount = (uint32_t)serialized.size();
    hdr.blockCount = (uint32_t)blocks.size();
    fwrite(&hdr, sizeof(hdr), 1, f);

    // Write blocks (compressed)
    std::vector<BlockInfo> blockInfoList;
    blockInfoList.reserve(blocks.size());
    for (auto& bk : blocks) {
        BlockInfo bi;
        memset(&bi, 0, sizeof(bi));
        bi.fileOffset = (uint64_t)ftell(f);
        bi.entityCount = (uint32_t)bk.offsets.size();

        // Compress block
        size_t maxComp = ZSTD_compressBound(bk.data.size());
        std::vector<uint8_t> compressed(maxComp);
        size_t compSize = ZSTD_compress(compressed.data(), maxComp,
                                        bk.data.data(), bk.data.size(), 3);
        if (ZSTD_isError(compSize)) { fclose(f); return false; }
        compressed.resize(compSize);

        bi.compressedSize = compSize;
        bi.uncompressedSize = bk.data.size();

        // Write block: compressedSize, uncompressedSize, compressed data
        fwrite(&bi.compressedSize, sizeof(bi.compressedSize), 1, f);
        fwrite(&bi.uncompressedSize, sizeof(bi.uncompressedSize), 1, f);
        fwrite(compressed.data(), 1, compSize, f);

        blockInfoList.push_back(bi);
    }

    // Write index section
    uint64_t indexOffset = (uint64_t)ftell(f);

    // Write BlockInfo entries
    for (auto& bi : blockInfoList) {
        fwrite(&bi, sizeof(bi), 1, f);
    }

    // Write EntityEntry entries
    uint32_t entityIdx = 0;
    for (uint32_t bi = 0; bi < (uint32_t)blocks.size(); bi++) {
        auto& bk = blocks[bi];
        for (uint32_t ei = 0; ei < (uint32_t)bk.offsets.size(); ei++) {
            EntityEntry ee;
            memset(&ee, 0, sizeof(ee));
            strncpy(ee.name, bk.names[ei].c_str(), sizeof(ee.name) - 1);
            ee.blockIndex = bi;
            ee.offsetInBlock = bk.offsets[ei];
            ee.size = bk.sizes[ei];
            fwrite(&ee, sizeof(ee), 1, f);
            entityIdx++;
        }
    }

    // Patch header with correct indexOffset
    hdr.indexOffset = indexOffset;
    fseek(f, 0, SEEK_SET);
    fwrite(&hdr, sizeof(hdr), 1, f);

    fclose(f);

    printf("[INFO] ExportArchive: %u block(s), %u entities\n",
           (uint32_t)blocks.size(), (uint32_t)serialized.size());
    return true;
}
#endif // EDITOR_BUILD

Scene::Scene()  { InitializeCriticalSection(&m_Lock); }
Scene::~Scene() { DeleteCriticalSection(&m_Lock); }

uint64_t Scene::CreateEntity(const std::string& name)
{
    uint64_t id = m_NextId++;
    m_Entities.emplace_back(id, name);
    MarkModified();
    return id;
}

bool Scene::RemoveEntity(uint64_t id)
{
    for (size_t i = 0; i < m_Entities.size(); i++) {
        if (m_Entities[i].id == id) {
            m_Entities.erase(m_Entities.begin() + i);
            MarkModified();
            return true;
        }
    }
    return false;
}

void Scene::InsertEntity(Entity e)
{
    m_Entities.push_back(std::move(e));
}

Entity* Scene::FindEntity(uint64_t id)
{
    for (auto& e : m_Entities) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

static void emitQuad(std::vector<float>& verts, std::vector<uint32_t>& indices,
                     const float v[4][3], const float n[3], const float c[3])
{
    int base = (int)verts.size() / VERTEX_STRIDE;
    for (int vi = 0; vi < 4; vi++) {
        verts.push_back(v[vi][0]); verts.push_back(v[vi][1]); verts.push_back(v[vi][2]);
        verts.push_back(n[0]);     verts.push_back(n[1]);     verts.push_back(n[2]);
        verts.push_back(c[0]);     verts.push_back(c[1]);     verts.push_back(c[2]);
    }
    indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
    indices.push_back(base + 0); indices.push_back(base + 2); indices.push_back(base + 3);
}

static void emitTri(std::vector<float>& verts, std::vector<uint32_t>& indices,
                    const float v[3][3], const float n[3], const float c[3])
{
    int base = (int)verts.size() / VERTEX_STRIDE;
    for (int vi = 0; vi < 3; vi++) {
        verts.push_back(v[vi][0]); verts.push_back(v[vi][1]); verts.push_back(v[vi][2]);
        verts.push_back(n[0]);     verts.push_back(n[1]);     verts.push_back(n[2]);
        verts.push_back(c[0]);     verts.push_back(c[1]);     verts.push_back(c[2]);
    }
    indices.push_back(base + 0); indices.push_back(base + 1); indices.push_back(base + 2);
}

void BuildCubeMesh(const float faceColors[6][3], std::vector<float>& verts, std::vector<uint32_t>& indices)
{
    static const float positions[6][4][3] = {
        {{-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}},
        {{ 1,-1,-1}, {-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1}},
        {{-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}, {-1, 1,-1}},
        {{ 1,-1, 1}, { 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}},
        {{-1, 1, 1}, { 1, 1, 1}, { 1, 1,-1}, {-1, 1,-1}},
        {{-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}, {-1,-1, 1}},
    };
    static const float normals[6][3] = {
        {0,0,1},{0,0,-1},{-1,0,0},{1,0,0},{0,1,0},{0,-1,0}
    };

    verts.clear();
    indices.clear();

    for (int face = 0; face < 6; face++) {
        float col[3] = { faceColors[face][0], faceColors[face][1], faceColors[face][2] };
        emitQuad(verts, indices, positions[face], normals[face], col);
    }
}

uint64_t CreateCubeEntity(Scene& scene)
{
    uint64_t id = scene.CreateEntity("Cube");
    Entity* e = scene.FindEntity(id);
    BuildCubeMesh(e->faceColors.colors, e->vertices, e->indices);
    e->meshDirty = true;
    return id;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void buildSphereMesh(std::vector<float>& verts, std::vector<uint32_t>& indices,
                            const float baseColor[3], float radius, int nlon, int nlat)
{
    verts.clear();
    indices.clear();
    for (int lat = 0; lat < nlat; lat++) {
        float theta1 = (float)M_PI * lat / nlat;
        float theta2 = (float)M_PI * (lat + 1) / nlat;
        float s1 = std::sin(theta1), c1 = std::cos(theta1);
        float s2 = std::sin(theta2), c2 = std::cos(theta2);
        for (int lon = 0; lon < nlon; lon++) {
            float phi1 = 2.0f * (float)M_PI * lon / nlon;
            float phi2 = 2.0f * (float)M_PI * (lon + 1) / nlon;
            float sp1 = std::sin(phi1), cp1 = std::cos(phi1);
            float sp2 = std::sin(phi2), cp2 = std::cos(phi2);
            float v[4][3] = {
                { radius*s2*cp2, radius*c2, radius*s2*sp2 },
                { radius*s2*cp1, radius*c2, radius*s2*sp1 },
                { radius*s1*cp1, radius*c1, radius*s1*sp1 },
                { radius*s1*cp2, radius*c1, radius*s1*sp2 },
            };
            float n[3];
            for (int i = 0; i < 3; i++) {
                float len = std::sqrt(v[0][0]*v[0][0] + v[0][1]*v[0][1] + v[0][2]*v[0][2]);
                n[i] = (len > 1e-10f) ? v[0][i] / len : 0.0f;
            }
            float bright = 0.4f + 0.6f * (1.0f - std::fabs(c1));
            float col[3] = { baseColor[0]*bright, baseColor[1]*bright, baseColor[2]*bright };
            emitQuad(verts, indices, v, n, col);
        }
    }
}

uint64_t CreateSphereEntity(Scene& scene)
{
    uint64_t id = scene.CreateEntity("Sphere");
    Entity* e = scene.FindEntity(id);
    float col[3] = { 0.3f, 0.5f, 1.0f };
    buildSphereMesh(e->vertices, e->indices, col, 1.0f, 16, 12);
    e->meshDirty = true;
    return id;
}

uint64_t CreateCapsuleEntity(Scene& scene)
{
    uint64_t id = scene.CreateEntity("Capsule");
    Entity* e = scene.FindEntity(id);
    auto& verts = e->vertices;
    auto& indices = e->indices;
    float r = 0.5f;
    float h = 1.0f;
    int segs = 16, stacks = 4;
    verts.clear(); indices.clear();
    float bodyCol[3] = { 0.2f, 0.8f, 0.3f };
    float topCol[3]  = { 0.3f, 0.9f, 0.4f };
    float botCol[3]  = { 0.1f, 0.7f, 0.2f };
    for (int i = 0; i < segs; i++) {
        float a1 = 2.0f * (float)M_PI * i / segs;
        float a2 = 2.0f * (float)M_PI * (i + 1) / segs;
        float sa1 = std::sin(a1), ca1 = std::cos(a1);
        float sa2 = std::sin(a2), ca2 = std::cos(a2);
        float v[4][3] = {
            { r*ca1, -h*0.5f, r*sa1 },
            { r*ca2, -h*0.5f, r*sa2 },
            { r*ca2,  h*0.5f, r*sa2 },
            { r*ca1,  h*0.5f, r*sa1 },
        };
        float n[3] = { ca1, 0.0f, sa1 };
        float len = std::sqrt(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        n[0] /= len; n[2] /= len;
        emitQuad(verts, indices, v, n, bodyCol);
    }
    for (int cap = 0; cap < 2; cap++) {
        float baseY = (cap == 0) ? -h*0.5f : h*0.5f;
        float* capCol = (cap == 0) ? botCol : topCol;
        float sign = (cap == 0) ? -1.0f : 1.0f;
        for (int stack = 0; stack < stacks; stack++) {
            float t1 = sign * (float)M_PI * 0.5f * stack / stacks;
            float t2 = sign * (float)M_PI * 0.5f * (stack + 1) / stacks;
            float s1 = std::sin(t1), c1 = std::cos(t1);
            float s2 = std::sin(t2), c2 = std::cos(t2);
            for (int i = 0; i < segs; i++) {
                float a1 = 2.0f * (float)M_PI * i / segs;
                float a2 = 2.0f * (float)M_PI * (i + 1) / segs;
                float sa1 = std::sin(a1), ca1 = std::cos(a1);
                float sa2 = std::sin(a2), ca2 = std::cos(a2);
                float off = (cap == 0) ? -1.0f : 0.0f;
                float v[4][3] = {
                    { r*s2*ca1, baseY + r*(c2+off), r*s2*sa1 },
                    { r*s2*ca2, baseY + r*(c2+off), r*s2*sa2 },
                    { r*s1*ca2, baseY + r*(c1+off), r*s1*sa2 },
                    { r*s1*ca1, baseY + r*(c1+off), r*s1*sa1 },
                };
                float cx = r*s1*ca1, cy = r*c1, cz = r*s1*sa1;
                float nl = std::sqrt(cx*cx + cy*cy + cz*cz);
                float n[3] = { cx/(nl>1e-10f?nl:1), cy/(nl>1e-10f?nl:1), cz/(nl>1e-10f?nl:1) };
                if (cap == 0) n[1] = -n[1];
                emitQuad(verts, indices, v, n, capCol);
            }
        }
    }
    e->meshDirty = true;
    return id;
}

uint64_t CreatePlaneEntity(Scene& scene)
{
    uint64_t id = scene.CreateEntity("Plane");
    Entity* e = scene.FindEntity(id);
    auto& verts = e->vertices;
    auto& indices = e->indices;
    verts.clear(); indices.clear();
    float positions[4][3] = {
        {-1.0f, 0.0f, -1.0f},
        { 1.0f, 0.0f, -1.0f},
        { 1.0f, 0.0f,  1.0f},
        {-1.0f, 0.0f,  1.0f},
    };
    float topCol[3]  = { 0.7f, 0.7f, 0.7f };
    float botCol[3]  = { 0.5f, 0.5f, 0.5f };
    float topN[3]  = { 0, 1, 0 };
    float botN[3]  = { 0, -1, 0 };
    emitQuad(verts, indices, positions, topN, topCol);
    float rev[4][3];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 3; j++)
            rev[i][j] = positions[3-i][j];
    emitQuad(verts, indices, rev, botN, botCol);
    e->meshDirty = true;
    return id;
}

uint64_t CreateTriangleEntity(Scene& scene)
{
    uint64_t id = scene.CreateEntity("Triangle");
    Entity* e = scene.FindEntity(id);
    auto& verts = e->vertices;
    auto& indices = e->indices;
    verts.clear(); indices.clear();
    float positions[3][3] = {
        { 0.0f, 0.0f,  1.0f},
        {-0.866f, 0.0f, -0.5f},
        { 0.866f, 0.0f, -0.5f},
    };
    float frontCol[3] = { 1.0f, 0.8f, 0.2f };
    float backCol[3]  = { 0.5f, 0.4f, 0.1f };
    float frontN[3] = { 0, 1, 0 };
    float backN[3]  = { 0, -1, 0 };
    emitTri(verts, indices, positions, frontN, frontCol);
    float rev[3][3];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            rev[i][j] = positions[2-i][j];
    emitTri(verts, indices, rev, backN, backCol);
    e->meshDirty = true;
    return id;
}

uint64_t CreateOctagonEntity(Scene& scene)
{
    uint64_t id = scene.CreateEntity("Octagon");
    Entity* e = scene.FindEntity(id);
    auto& verts = e->vertices;
    auto& indices = e->indices;
    int n = 8;
    verts.clear(); indices.clear();
    float halfH = 0.5f;
    float sideCol[3] = { 0.6f, 0.2f, 0.6f };
    float topCol[3]  = { 0.7f, 0.3f, 0.7f };
    float botCol[3]  = { 0.5f, 0.1f, 0.5f };
    for (int i = 0; i < n; i++) {
        float a1 = 2.0f * (float)M_PI * i / n;
        float a2 = 2.0f * (float)M_PI * (i + 1) / n;
        float ca1 = std::cos(a1), sa1 = std::sin(a1);
        float ca2 = std::cos(a2), sa2 = std::sin(a2);
        float v[4][3] = {
            { ca1, -halfH, sa1 },
            { ca2, -halfH, sa2 },
            { ca2,  halfH, sa2 },
            { ca1,  halfH, sa1 },
        };
        float nx = ca1, nz = sa1;
        float nl = std::sqrt(nx*nx + nz*nz);
        float sideN[3] = { nx/(nl>1e-10f?nl:1), 0, nz/(nl>1e-10f?nl:1) };
        emitQuad(verts, indices, v, sideN, sideCol);
    }
    float topPts[8][3], botPts[8][3];
    for (int i = 0; i < n; i++) {
        float a = 2.0f * (float)M_PI * i / n;
        topPts[i][0] = std::cos(a); topPts[i][1] = halfH;  topPts[i][2] = std::sin(a);
        botPts[i][0] = std::cos(a); botPts[i][1] = -halfH; botPts[i][2] = std::sin(a);
    }
    float topN[3] = { 0, 1, 0 }, botN[3] = { 0, -1, 0 };
    for (int i = 1; i < n-1; i++) {
        float tri[3][3];
        tri[0][0] = topPts[0][0]; tri[0][1] = topPts[0][1]; tri[0][2] = topPts[0][2];
        tri[1][0] = topPts[i+1][0]; tri[1][1] = topPts[i+1][1]; tri[1][2] = topPts[i+1][2];
        tri[2][0] = topPts[i][0]; tri[2][1] = topPts[i][1]; tri[2][2] = topPts[i][2];
        emitTri(verts, indices, tri, topN, topCol);
    }
    for (int i = 1; i < n-1; i++) {
        float tri[3][3];
        tri[0][0] = botPts[0][0]; tri[0][1] = botPts[0][1]; tri[0][2] = botPts[0][2];
        tri[1][0] = botPts[i+1][0]; tri[1][1] = botPts[i+1][1]; tri[1][2] = botPts[i+1][2];
        tri[2][0] = botPts[i][0]; tri[2][1] = botPts[i][1]; tri[2][2] = botPts[i][2];
        emitTri(verts, indices, tri, botN, botCol);
    }
    e->meshDirty = true;
    return id;
}

uint64_t CreateSnowmanEntity(Scene& scene)
{
    uint64_t id = scene.CreateEntity("Snowman");
    Entity* e = scene.FindEntity(id);
    auto& verts = e->vertices;
    auto& indices = e->indices;
    verts.clear(); indices.clear();
    struct { float radius, y; float color[3]; } parts[3] = {
        { 0.8f, -0.8f, { 1.0f, 1.0f, 1.0f } },
        { 0.55f, 0.0f, { 0.95f, 0.95f, 0.95f } },
        { 0.35f, 0.7f, { 0.9f, 0.9f, 0.9f } },
    };
    for (int p = 0; p < 3; p++) {
        float radius = parts[p].radius;
        float cy = parts[p].y;
        float* baseColor = parts[p].color;
        int nlon = 12, nlat = 8;
        for (int lat = 0; lat < nlat; lat++) {
            float theta1 = (float)M_PI * lat / nlat;
            float theta2 = (float)M_PI * (lat + 1) / nlat;
            float s1 = std::sin(theta1), c1 = std::cos(theta1);
            float s2 = std::sin(theta2), c2 = std::cos(theta2);
            for (int lon = 0; lon < nlon; lon++) {
                float phi1 = 2.0f * (float)M_PI * lon / nlon;
                float phi2 = 2.0f * (float)M_PI * (lon + 1) / nlon;
                float sp1 = std::sin(phi1), cp1 = std::cos(phi1);
                float sp2 = std::sin(phi2), cp2 = std::cos(phi2);
                float v[4][3] = {
                    { radius*s2*cp2, cy + radius*c2, radius*s2*sp2 },
                    { radius*s2*cp1, cy + radius*c2, radius*s2*sp1 },
                    { radius*s1*cp1, cy + radius*c1, radius*s1*sp1 },
                    { radius*s1*cp2, cy + radius*c1, radius*s1*sp2 },
                };
                float nx = v[0][0] - 0, ny = v[0][1] - cy, nz = v[0][2] - 0;
                float nl = std::sqrt(nx*nx + ny*ny + nz*nz);
                float n[3] = { nx/(nl>1e-10f?nl:1), ny/(nl>1e-10f?nl:1), nz/(nl>1e-10f?nl:1) };
                float bright = 0.5f + 0.5f * (1.0f - std::fabs(c1));
                float col[3] = { baseColor[0]*bright, baseColor[1]*bright, baseColor[2]*bright };
                emitQuad(verts, indices, v, n, col);
            }
        }
    }
    e->meshDirty = true;
    return id;
}

#ifdef EDITOR_BUILD
void Scene::PushUndo(UndoEntry::Type type, const Entity& e)
{
    if (m_UndoStack.size() >= MAX_UNDO)
        m_UndoStack.erase(m_UndoStack.begin());
    UndoEntry ue;
    ue.type = type;
    ue.entity = e;
    m_UndoStack.push_back(std::move(ue));
}

void Scene::PushUndo(UndoEntry entry)
{
    if (m_UndoStack.size() >= MAX_UNDO)
        m_UndoStack.erase(m_UndoStack.begin());
    m_UndoStack.push_back(std::move(entry));
}

UndoEntry Scene::PopUndo()
{
    UndoEntry e = std::move(m_UndoStack.back());
    m_UndoStack.pop_back();
    return e;
}

UndoEntry Scene::PopRedo()
{
    UndoEntry e = std::move(m_RedoStack.back());
    m_RedoStack.pop_back();
    return e;
}

void Scene::PushRedo(UndoEntry entry)
{
    if (m_RedoStack.size() >= MAX_UNDO)
        m_RedoStack.erase(m_RedoStack.begin());
    m_RedoStack.push_back(std::move(entry));
}

void Scene::ReparentEntity(uint64_t entityId, uint64_t newParentId)
{
    Entity* e = FindEntity(entityId);
    if (!e) return;
    // Prevent circular parenting
    if (newParentId != 0 && IsChildOf(newParentId, entityId)) return;
    // Don't reparent to self
    if (entityId == newParentId) return;
    e->parentId = newParentId;
}

std::vector<Entity*> Scene::GetChildren(uint64_t parentId)
{
    std::vector<Entity*> children;
    for (auto& e : m_Entities) {
        if (e.parentId == parentId)
            children.push_back(&e);
    }
    return children;
}

bool Scene::IsChildOf(uint64_t entityId, uint64_t potentialParent)
{
    if (potentialParent == 0) return false;
    const Entity* e = FindEntity(entityId);
    while (e && e->parentId != 0) {
        if (e->parentId == potentialParent) return true;
        e = FindEntity(e->parentId);
    }
    return false;
}
#endif // EDITOR_BUILD

Entity* Scene::FindServerService()
{
    for (auto& e : m_Entities) {
        if (e.HasFlag(ENTITY_SERVER_SERVICE)) return &e;
    }
    return nullptr;
}

Entity* Scene::FindWorldEnvironment()
{
    for (auto& e : m_Entities) {
        if (e.HasFlag(ENTITY_WORLD_ENV)) return &e;
    }
    return nullptr;
}

XMMATRIX ComputeWorldMatrix(const TransformComponent& tc)
{
    XMMATRIX s = XMMatrixScaling(tc.scale.x, tc.scale.y, tc.scale.z);
    XMMATRIX r = XMMatrixRotationRollPitchYaw(tc.rotation.x, tc.rotation.y, tc.rotation.z);
    XMMATRIX t = XMMatrixTranslation(tc.position.x, tc.position.y, tc.position.z);
    return s * r * t;
}