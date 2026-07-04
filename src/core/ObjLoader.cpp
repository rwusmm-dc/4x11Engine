#include "ObjLoader.h"
#include "ecs/ECS.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <unordered_map>
#include <cstring>

struct Vec3 { float x, y, z; };

static Vec3 FaceNormal(const Vec3& a, const Vec3& b, const Vec3& c)
{
    Vec3 e1 = { b.x - a.x, b.y - a.y, b.z - a.z };
    Vec3 e2 = { c.x - a.x, c.y - a.y, c.z - a.z };
    Vec3 n  = { e1.y * e2.z - e1.z * e2.y,
                e1.z * e2.x - e1.x * e2.z,
                e1.x * e2.y - e1.y * e2.x };
    float len = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
    if (len > 1e-8f) { n.x /= len; n.y /= len; n.z /= len; }
    return n;
}

static uint64_t MakeKey(int pi, float nx, float ny, float nz)
{
    int ix = (int)((nx * 0.5f + 0.5f) * 1023.0f);
    int iy = (int)((ny * 0.5f + 0.5f) * 1023.0f);
    int iz = (int)((nz * 0.5f + 0.5f) * 1023.0f);
    if (ix < 0) ix = 0;
    if (ix > 1023) ix = 1023;
    if (iy < 0) iy = 0;
    if (iy > 1023) iy = 1023;
    if (iz < 0) iz = 0;
    if (iz > 1023) iz = 1023;
    return ((uint64_t)pi << 30) | ((uint64_t)ix << 20) | ((uint64_t)iy << 10) | (uint64_t)iz;
}

struct FaceVert { int pi; };

bool LoadObj(const char* filename, ObjMesh& out)
{
    HANDLE file = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(file, nullptr);
    HANDLE map = CreateFileMappingA(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(file);
    if (!map) return false;

    const char* data = (const char*)MapViewOfFile(map, FILE_MAP_READ, 0, 0, size);
    if (!data) { CloseHandle(map); return false; }

    int lineCount = 0;
    for (DWORD i = 0; i < size; i++)
        if (data[i] == '\n') lineCount++;

    std::vector<Vec3> positions;
    positions.reserve(lineCount / 4);

    std::vector<std::vector<FaceVert>> faces;
    faces.reserve(lineCount / 6);

    const char* ptr = data;
    const char* end = data + size;
    char line[512];

    while (ptr < end) {
        const char* nl = (const char*)memchr(ptr, '\n', end - ptr);
        int len = nl ? (int)(nl - ptr) : (int)(end - ptr);
        if (len > 511) len = 511;
        memcpy(line, ptr, len);
        line[len] = '\0';

        if (line[0] == 'v' && (line[1] == ' ' || line[1] == '\t')) {
            Vec3 p;
            if (sscanf(line + 1, "%f %f %f", &p.x, &p.y, &p.z) >= 3)
                positions.push_back(p);
        }
        else if (line[0] == 'f' && (line[1] == ' ' || line[1] == '\t')) {
            int pi[4] = { -1, -1, -1, -1 };
            int count = 0;
            char* c = line + 2;
            while (*c == ' ' || *c == '\t') c++;
            while (*c && *c != '\r' && *c != '\n') {
                int val = 0;
                while (*c >= '0' && *c <= '9') { val = val * 10 + (*c - '0'); c++; }
                if (*c == '/') c++;
                while (*c >= '0' && *c <= '9') c++;
                if (*c == '/') c++;
                while (*c >= '0' && *c <= '9') c++;
                if (val > 0 && count < 4)
                    pi[count++] = val - 1;
                while (*c == ' ' || *c == '\t') c++;
            }
            if (count >= 3) {
                std::vector<FaceVert> fv;
                fv.reserve(count);
                for (int i = 0; i < count; i++)
                    fv.push_back({ pi[i] });
                faces.push_back(std::move(fv));
            }
        }

        ptr = nl ? (nl + 1) : end;
    }

    UnmapViewOfFile(data);
    CloseHandle(map);

    if (faces.empty() || positions.empty()) return false;

    std::unordered_map<uint64_t, int> vertMap;
    std::vector<float> verts;
    verts.reserve(positions.size() * 9);
    std::vector<uint32_t> idx;
    idx.reserve(faces.size() * 3);

    for (const auto& face : faces) {
        int count = (int)face.size();
        for (int t = 1; t < count - 1; t++) {
            int i0 = face[0].pi;
            int i1 = face[t].pi;
            int i2 = face[t + 1].pi;

            Vec3 fn = FaceNormal(positions[i0], positions[i1], positions[i2]);

            int tri[3] = { i0, i1, i2 };
            for (int k = 0; k < 3; k++) {
                int pi = tri[k];
                uint64_t key = MakeKey(pi, fn.x, fn.y, fn.z);
                auto it = vertMap.find(key);
                if (it != vertMap.end()) {
                    idx.push_back(it->second);
                } else {
                    int vi = (int)vertMap.size();
                    vertMap[key] = vi;
                    verts.push_back(positions[pi].x);
                    verts.push_back(positions[pi].y);
                    verts.push_back(positions[pi].z);
                    verts.push_back(fn.x);
                    verts.push_back(fn.y);
                    verts.push_back(fn.z);
                    verts.push_back(0.7f);
                    verts.push_back(0.7f);
                    verts.push_back(0.7f);
                    idx.push_back(vi);
                }
            }
        }
    }

    out.vertices   = std::move(verts);
    out.indices    = std::move(idx);
    out.vertCount  = (int)(out.vertices.size() / VERTEX_STRIDE);
    out.indexCount = (int)out.indices.size();
    return true;
}
