#include "SdkMeshLoader.h"
#include "ObjLoader.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

static bool ReadFileData(const char* filename, std::vector<unsigned char>& data)
{
    HANDLE file = CreateFileA(filename, GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    DWORD size = GetFileSize(file, nullptr);
    if (size == 0 || size == INVALID_FILE_SIZE) {
        CloseHandle(file);
        return false;
    }

    data.resize(size);
    DWORD bytesRead;
    bool result = ReadFile(file, data.data(), size, &bytesRead, nullptr) && bytesRead == size;
    CloseHandle(file);
    return result;
}

static bool ParseSdkMesh(const unsigned char* data, size_t size, SdkMeshHeader& header,
                         std::vector<SdkMesh>& meshes, std::vector<SdkMaterial>& materials,
                         std::vector<float>& vertices, std::vector<uint32_t>& indices)
{
    if (size < sizeof(SdkMeshHeader)) return false;

    const SdkMeshHeader* hdr = reinterpret_cast<const SdkMeshHeader*>(data);
    header = *hdr;

    if (memcmp(hdr->Magic, "SDKMESH", 8) != 0) return false;
    if (memcmp(hdr->Version, "1.00", 4) != 0 && memcmp(hdr->Version, "1.01", 4) != 0) return false;

    if (hdr->NumMeshes == 0 || hdr->NumVertices == 0 || hdr->NumIndices == 0) return false;

    if (hdr->MeshOffset + hdr->NumMeshes * sizeof(SdkMesh) > size) return false;
    const SdkMesh* meshData = reinterpret_cast<const SdkMesh*>(data + hdr->MeshOffset);
    meshes.resize(hdr->NumMeshes);
    for (uint32_t i = 0; i < hdr->NumMeshes; ++i) {
        meshes[i] = meshData[i];
    }

    if (hdr->MaterialsOffset > 0 && hdr->NumMaterials > 0) {
        if (hdr->MaterialsOffset + hdr->NumMaterials * sizeof(SdkMaterial) > size) return false;
        const SdkMaterial* matData = reinterpret_cast<const SdkMaterial*>(data + hdr->MaterialsOffset);
        materials.resize(hdr->NumMaterials);
        for (uint32_t i = 0; i < hdr->NumMaterials; ++i) {
            materials[i] = matData[i];
        }
    }

    if (hdr->VerticesOffset + hdr->NumVertices * 3 * sizeof(float) > size) return false;
    vertices.resize(hdr->NumVertices * 3);
    const float* vertData = reinterpret_cast<const float*>(data + hdr->VerticesOffset);
    for (uint32_t i = 0; i < hdr->NumVertices * 3; ++i) {
        vertices[i] = vertData[i];
    }

    if (hdr->IndicesOffset + hdr->NumIndices * sizeof(uint32_t) > size) {
        if (hdr->IndicesOffset + hdr->NumIndices * sizeof(uint16_t) > size) return false;
        const uint16_t* idx16 = reinterpret_cast<const uint16_t*>(data + hdr->IndicesOffset);
        indices.resize(hdr->NumIndices);
        for (uint32_t i = 0; i < hdr->NumIndices; ++i) {
            indices[i] = static_cast<uint32_t>(idx16[i]);
        }
    } else {
        const uint32_t* idx32 = reinterpret_cast<const uint32_t*>(data + hdr->IndicesOffset);
        indices.resize(hdr->NumIndices);
        for (uint32_t i = 0; i < hdr->NumIndices; ++i) {
            indices[i] = idx32[i];
        }
    }

    return true;
}

static void AddVertexData(std::vector<float>& outVerts, const float* pos, const float* normal,
                          const float* color)
{
    outVerts.push_back(pos[0]);
    outVerts.push_back(pos[1]);
    outVerts.push_back(pos[2]);

    if (normal) {
        outVerts.push_back(normal[0]);
        outVerts.push_back(normal[1]);
        outVerts.push_back(normal[2]);
    } else {
        outVerts.push_back(0.0f);
        outVerts.push_back(1.0f);
        outVerts.push_back(0.0f);
    }

    if (color) {
        outVerts.push_back(color[0]);
        outVerts.push_back(color[1]);
        outVerts.push_back(color[2]);
    } else {
        outVerts.push_back(0.7f);
        outVerts.push_back(0.7f);
        outVerts.push_back(0.7f);
    }
}

static SdkMeshResult ConvertSdkMeshToResult(const std::vector<float>& positions,
                                             const std::vector<uint32_t>& indices,
                                             const std::vector<SdkMaterial>& materials,
                                             const float* defaultColor)
{
    SdkMeshResult result;
    const int VERTEX_STRIDE = 9;

    if (positions.empty() || indices.empty()) {
        return result;
    }

    std::vector<float> outVerts;
    outVerts.reserve(positions.size() * 3);

    std::vector<std::vector<uint32_t>> vertexFaces(positions.size() / 3);
    std::vector<float> normalSum(positions.size(), 0.0f);

    for (size_t i = 0; i < indices.size(); i += 3) {
        if (i + 2 >= indices.size()) break;

        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        if (i0 >= positions.size() / 3 || i1 >= positions.size() / 3 || i2 >= positions.size() / 3)
            continue;

        const float* p0 = &positions[i0 * 3];
        const float* p1 = &positions[i1 * 3];
        const float* p2 = &positions[i2 * 3];

        float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
        float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
        float n[3] = {
            e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2],
            e1[0] * e2[1] - e1[1] * e2[0]
        };
        float len = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
        if (len > 1e-8f) {
            n[0] /= len; n[1] /= len; n[2] /= len;
        }

        vertexFaces[i0].push_back(i / 3);
        vertexFaces[i1].push_back(i / 3);
        vertexFaces[i2].push_back(i / 3);
        normalSum[i0 * 3 + 0] += n[0];
        normalSum[i0 * 3 + 1] += n[1];
        normalSum[i0 * 3 + 2] += n[2];
        normalSum[i1 * 3 + 0] += n[0];
        normalSum[i1 * 3 + 1] += n[1];
        normalSum[i1 * 3 + 2] += n[2];
        normalSum[i2 * 3 + 0] += n[0];
        normalSum[i2 * 3 + 1] += n[1];
        normalSum[i2 * 3 + 2] += n[2];
    }

    for (size_t i = 0; i < positions.size() / 3; ++i) {
        const float* pos = &positions[i * 3];
        float* normal = &normalSum[i * 3];
        float len = sqrtf(normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
        float n[3] = { 0.0f, 1.0f, 0.0f };
        if (len > 1e-8f) {
            n[0] = normal[0] / len;
            n[1] = normal[1] / len;
            n[2] = normal[2] / len;
        }

        float col[3] = { 0.7f, 0.7f, 0.7f };
        if (defaultColor) {
            col[0] = defaultColor[0];
            col[1] = defaultColor[1];
            col[2] = defaultColor[2];
        }
        if (!materials.empty()) {
            const SdkMaterial& mat = materials[0];
            uint32_t diff = mat.DiffuseColor[0];
            col[0] = ((diff >> 16) & 0xFF) / 255.0f;
            col[1] = ((diff >> 8) & 0xFF) / 255.0f;
            col[2] = (diff & 0xFF) / 255.0f;
        }

        AddVertexData(outVerts, pos, n, col);
    }

    result.Vertices = std::move(outVerts);
    result.Indices = indices;
    result.VertexCount = static_cast<int>(result.Vertices.size() / VERTEX_STRIDE);
    result.IndexCount = static_cast<int>(result.Indices.size());
    result.Success = true;

    return result;
}

bool LoadSdkMeshFile(const char* filename, SdkMeshResult& out, const float* defaultColor)
{
    std::vector<unsigned char> fileData;
    if (!ReadFileData(filename, fileData)) {
        return false;
    }

    SdkMeshHeader header;
    std::vector<SdkMesh> meshes;
    std::vector<SdkMaterial> materials;
    std::vector<float> positions;
    std::vector<uint32_t> indices;

    if (!ParseSdkMesh(fileData.data(), fileData.size(), header, meshes, materials,
                      positions, indices)) {
        return false;
    }

    SdkMeshResult result = ConvertSdkMeshToResult(positions, indices, materials, defaultColor);
    if (!result.Success) {
        return false;
    }

    out = std::move(result);
    return true;
}

bool LoadSdkMeshAsObj(const char* filename, ObjMesh& out)
{
    SdkMeshResult result;
    if (!LoadSdkMeshFile(filename, result)) {
        return false;
    }

    out.vertices = std::move(result.Vertices);
    out.indices = std::move(result.Indices);
    out.vertCount = result.VertexCount;
    out.indexCount = result.IndexCount;
    return true;
}