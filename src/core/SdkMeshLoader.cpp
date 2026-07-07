#include "SdkMeshLoader.h"
#include "ObjLoader.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <unordered_map>

// D3DDECLTYPE values
enum D3DDeclType {
    D3D_FLOAT1   = 0,
    D3D_FLOAT2   = 1,
    D3D_FLOAT3   = 2,
    D3D_FLOAT4   = 3,
    D3D_D3DCOLOR = 4,
    D3D_UBYTE4   = 5,
    D3D_SHORT2   = 6,
    D3D_SHORT4   = 7,
    D3D_UBYTE4N  = 8,
    D3D_SHORT2N  = 9,
    D3D_SHORT4N  = 10,
    D3D_USHORT2N = 11,
    D3D_USHORT4N = 12,
    D3D_UDEC3    = 13,
    D3D_DEC3N    = 14,
    D3D_FLOAT16_2 = 15,
    D3D_FLOAT16_4 = 16,
    D3D_UNUSED   = 17,
};

// D3DDECLUSAGE values
enum D3DDeclUsage {
    D3D_POSITION0 = 0,
    D3D_BLENDWEIGHT = 1,
    D3D_BLENDINDICES = 2,
    D3D_NORMAL0 = 3,
    D3D_TEXCOORD0 = 5,
    D3D_TANGENT0 = 6,
    D3D_BINORMAL0 = 7,
    D3D_COLOR0 = 10,
};

static int DeclTypeSize(uint8_t type) {
    switch (type) {
        case D3D_FLOAT1:   return 4;
        case D3D_FLOAT2:   return 8;
        case D3D_FLOAT3:   return 12;
        case D3D_FLOAT4:   return 16;
        case D3D_D3DCOLOR: return 4;
        case D3D_UBYTE4:   return 4;
        case D3D_SHORT2:   return 4;
        case D3D_SHORT4:   return 8;
        case D3D_UBYTE4N:  return 4;
        case D3D_SHORT2N:  return 4;
        case D3D_SHORT4N:  return 8;
        case D3D_USHORT2N: return 4;
        case D3D_USHORT4N: return 8;
        case D3D_UDEC3:    return 4;
        case D3D_DEC3N:    return 4;
        case D3D_FLOAT16_2: return 4;
        case D3D_FLOAT16_4: return 8;
        default:            return 4;
    }
}

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

static const uint8_t* ReadOffset(const uint8_t* base, size_t fileSize, uint64_t offset, size_t minSize)
{
    if (offset > fileSize || fileSize - offset < minSize)
        return nullptr;
    return base + offset;
}

template<typename T>
static const T* ReadStruct(const uint8_t* base, size_t fileSize, uint64_t offset)
{
    return reinterpret_cast<const T*>(ReadOffset(base, fileSize, offset, sizeof(T)));
}

static bool ParseSdkMesh(const uint8_t* data, size_t size,
                         std::vector<float>& outVerts, std::vector<uint32_t>& outIndices,
                         const float* defaultColor)
{
    // Validate magic
    if (size < sizeof(SdkMeshHeader)) return false;
    if (memcmp(data, "SDKMESH", 8) != 0) return false;

    const SdkMeshHeader* hdr = reinterpret_cast<const SdkMeshHeader*>(data);
    if (hdr->HeaderSize > size) return false;

    if (hdr->NumVertexBuffers == 0 || hdr->NumIndexBuffers == 0 || hdr->NumMeshes == 0)
        return false;

    // Compute the end of header area — this is where raw buffer data starts
    uint64_t bufferDataBase = hdr->HeaderSize + hdr->NonBufferDataSize;

    // Each VB header is followed by 32 D3DDeclElement entries (8 bytes each).
    // Total per VB: sizeof(SdkMeshVertexBufferHeader) + 32 * sizeof(D3DDeclElement) = 288 bytes.
    const uint64_t vbHeaderStride = sizeof(SdkMeshVertexBufferHeader) + 32 * sizeof(D3DDeclElement);

    struct VBInfo {
        uint64_t dataOffset;
        uint64_t stride;
        uint64_t numVerts;
        int posOffset = -1;
        int normOffset = -1;
    };
    std::vector<VBInfo> vbInfos;

    for (uint32_t i = 0; i < hdr->NumVertexBuffers; i++) {
        uint64_t off = hdr->VertexStreamHeadersOffset + i * vbHeaderStride;
        const SdkMeshVertexBufferHeader* vbh = ReadStruct<SdkMeshVertexBufferHeader>(data, size, off);
        if (!vbh) continue;

        VBInfo vb;
        vb.dataOffset = vbh->DataOffset;
        vb.stride = vbh->StrideBytes;
        vb.numVerts = vbh->NumVertices;

        // Parse vertex declaration (32 D3DDeclElement entries follow the header)
        for (int e = 0; e < 32; e++) {
            uint64_t declOff = off + sizeof(SdkMeshVertexBufferHeader) + e * sizeof(D3DDeclElement);
            const D3DDeclElement* decl = ReadStruct<D3DDeclElement>(data, size, declOff);
            if (!decl) break;
            if (decl->Type == D3D_UNUSED || decl->Usage == 0xFF) break;

            if (decl->Usage == D3D_POSITION0 && decl->UsageIndex == 0) {
                vb.posOffset = decl->Offset;
            } else if (decl->Usage == D3D_NORMAL0 && decl->UsageIndex == 0) {
                vb.normOffset = decl->Offset;
            }
        }

        vbInfos.push_back(vb);
    }

    const uint64_t ibHeaderStride = sizeof(SdkMeshIndexBufferHeader);

    struct IBInfo {
        uint64_t dataOffset;
        uint64_t numIndices;
        int indexType; // 2 = 16-bit, 4 = 32-bit
    };
    std::vector<IBInfo> ibInfos;

    for (uint32_t i = 0; i < hdr->NumIndexBuffers; i++) {
        uint64_t off = hdr->IndexStreamHeadersOffset + i * ibHeaderStride;
        const SdkMeshIndexBufferHeader* ibh = ReadStruct<SdkMeshIndexBufferHeader>(data, size, off);
        if (!ibh) continue;

        IBInfo ib;
        ib.dataOffset = ibh->DataOffset;
        ib.numIndices = ibh->NumIndices;
        ib.indexType = (ibh->IndexType == 0) ? 2 : 4;
        ibInfos.push_back(ib);
    }

    if (vbInfos.empty() || ibInfos.empty()) return false;

    // Collect all geometry and merge into one output mesh.
    // Track unique vertex positions for deduplication.
    struct MergedVertex {
        float pos[3];
        float norm[3];
    };
    std::vector<MergedVertex> mergedVerts;
    std::vector<uint32_t> mergedIndices;
    std::unordered_map<uint64_t, uint32_t> vertMap; // (vbIdx << 32 | vbVertIdx) -> merged index

    struct TempFace {
        uint32_t i0, i1, i2;
    };
    std::vector<TempFace> faces;
    std::vector<float> tempPositions;
    std::vector<float> tempNormals;

    uint64_t meshArrayOff = hdr->MeshDataOffset;
    for (uint32_t m = 0; m < hdr->NumMeshes; m++) {
        uint64_t meshOff = meshArrayOff + m * sizeof(SdkMeshMesh);
        const SdkMeshMesh* mesh = ReadStruct<SdkMeshMesh>(data, size, meshOff);
        if (!mesh || mesh->NumVertexBuffers == 0) continue;

        // Get VB and IB indices
        uint32_t vbIdx = mesh->VertexBuffers[0];
        uint32_t ibIdx = mesh->IndexBuffer;
        if (vbIdx >= vbInfos.size() || ibIdx >= ibInfos.size()) continue;

        const VBInfo& vb = vbInfos[vbIdx];
        const IBInfo& ib = ibInfos[ibIdx];

        if (vb.posOffset < 0) continue; // no position data

        // Read subset indices array
        // Each mesh has an array of subset indices at SubsetOffset
        if (mesh->SubsetOffset == 0 || mesh->NumSubsets == 0) continue;

        for (uint32_t s = 0; s < mesh->NumSubsets; s++) {
            uint32_t subsetIdx;
            uint64_t subsetIdxOff = mesh->SubsetOffset + s * sizeof(uint32_t);
            const uint32_t* sp = reinterpret_cast<const uint32_t*>(ReadOffset(data, size, subsetIdxOff, sizeof(uint32_t)));
            if (!sp) continue;
            subsetIdx = *sp;

            if (subsetIdx >= hdr->NumSubsets) continue;

            // Read the subset
            uint64_t subsetOff = hdr->SubsetDataOffset + subsetIdx * sizeof(SdkMeshSubset);
            const SdkMeshSubset* subset = ReadStruct<SdkMeshSubset>(data, size, subsetOff);
            if (!subset || subset->IndexCount < 3) continue;

            uint64_t indexSize = ib.indexType; // 2 or 4 bytes per index
            const uint8_t* indexBase = ReadOffset(data, size, ib.dataOffset, (subset->IndexStart + subset->IndexCount) * indexSize);
            if (!indexBase) continue;

            // Read vertex data for this subset
            const uint8_t* vertBase = ReadOffset(data, size, vb.dataOffset, (subset->VertexStart + subset->VertexCount) * vb.stride);
            if (!vertBase) continue;

            // Process each triangle in the subset
            for (uint64_t i = 0; i + 2 < subset->IndexCount; i += 3) {
                uint32_t vi[3];
                const uint8_t* iptr = indexBase + subset->IndexStart * indexSize;
                if (ib.indexType == 2) {
                    const uint16_t* idx16 = reinterpret_cast<const uint16_t*>(iptr);
                    vi[0] = idx16[i] + (uint32_t)subset->VertexStart;
                    vi[1] = idx16[i + 1] + (uint32_t)subset->VertexStart;
                    vi[2] = idx16[i + 2] + (uint32_t)subset->VertexStart;
                } else {
                    const uint32_t* idx32 = reinterpret_cast<const uint32_t*>(iptr);
                    vi[0] = idx32[i] + (uint32_t)subset->VertexStart;
                    vi[1] = idx32[i + 1] + (uint32_t)subset->VertexStart;
                    vi[2] = idx32[i + 2] + (uint32_t)subset->VertexStart;
                }

                // Read vertex positions
                float pos[3][3];
                for (int v = 0; v < 3; v++) {
                    const uint8_t* vertPtr = vertBase + (vi[v] - subset->VertexStart) * vb.stride;
                    const float* pf = reinterpret_cast<const float*>(vertPtr + vb.posOffset);
                    pos[v][0] = pf[0];
                    pos[v][1] = pf[1];
                    pos[v][2] = pf[2];

                    // Store position for later use
                    tempPositions.push_back(pos[v][0]);
                    tempPositions.push_back(pos[v][1]);
                    tempPositions.push_back(pos[v][2]);
                }

                // Read normals if available, or compute from face
                float norm[3][3];
                bool hasNormals = (vb.normOffset >= 0);
                if (hasNormals) {
                    for (int v = 0; v < 3; v++) {
                        const uint8_t* vertPtr = vertBase + (vi[v] - subset->VertexStart) * vb.stride;
                        const float* nf = reinterpret_cast<const float*>(vertPtr + vb.normOffset);
                        norm[v][0] = nf[0];
                        norm[v][1] = nf[1];
                        norm[v][2] = nf[2];
                        float nl = sqrtf(norm[v][0]*norm[v][0] + norm[v][1]*norm[v][1] + norm[v][2]*norm[v][2]);
                        if (nl > 1e-8f) { norm[v][0] /= nl; norm[v][1] /= nl; norm[v][2] /= nl; }
                    }
                } else {
                    // Compute face normal
                    float e1[3] = { pos[1][0]-pos[0][0], pos[1][1]-pos[0][1], pos[1][2]-pos[0][2] };
                    float e2[3] = { pos[2][0]-pos[0][0], pos[2][1]-pos[0][1], pos[2][2]-pos[0][2] };
                    float fn[3] = {
                        e1[1]*e2[2] - e1[2]*e2[1],
                        e1[2]*e2[0] - e1[0]*e2[2],
                        e1[0]*e2[1] - e1[1]*e2[0]
                    };
                    float fl = sqrtf(fn[0]*fn[0] + fn[1]*fn[1] + fn[2]*fn[2]);
                    if (fl > 1e-8f) { fn[0] /= fl; fn[1] /= fl; fn[2] /= fl; }
                    if (fn[1] < 0) { fn[0] = -fn[0]; fn[1] = -fn[1]; fn[2] = -fn[2]; }
                    for (int v = 0; v < 3; v++) {
                        norm[v][0] = fn[0]; norm[v][1] = fn[1]; norm[v][2] = fn[2];
                    }
                }

                // Store normals
                for (int v = 0; v < 3; v++) {
                    tempNormals.push_back(norm[v][0]);
                    tempNormals.push_back(norm[v][1]);
                    tempNormals.push_back(norm[v][2]);
                }

                faces.push_back({(uint32_t)(tempPositions.size()/3 - 3),
                                 (uint32_t)(tempPositions.size()/3 - 2),
                                 (uint32_t)(tempPositions.size()/3 - 1)});
            }
        }
    }

    if (tempPositions.empty() || faces.empty()) return false;

    // Compute smoothed normals if per-vertex normals were not provided
    if (tempNormals.empty()) {
        tempNormals.resize(tempPositions.size(), 0.0f);
        for (auto& f : faces) {
            float* p0 = &tempPositions[f.i0 * 3];
            float* p1 = &tempPositions[f.i1 * 3];
            float* p2 = &tempPositions[f.i2 * 3];
            float e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
            float e2[3] = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
            float fn[3] = {
                e1[1]*e2[2] - e1[2]*e2[1],
                e1[2]*e2[0] - e1[0]*e2[2],
                e1[0]*e2[1] - e1[1]*e2[0]
            };
            float fl = sqrtf(fn[0]*fn[0] + fn[1]*fn[1] + fn[2]*fn[2]);
            if (fl > 1e-8f) { fn[0] /= fl; fn[1] /= fl; fn[2] /= fl; }
            for (int v = 0; v < 3; v++) {
                tempNormals[f.i0 * 3 + v] += fn[v];
                tempNormals[f.i1 * 3 + v] += fn[v];
                tempNormals[f.i2 * 3 + v] += fn[v];
            }
        }
        for (size_t i = 0; i < tempNormals.size() / 3; i++) {
            float* n = &tempNormals[i * 3];
            float nl = sqrtf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2]);
            if (nl > 1e-8f) { n[0] /= nl; n[1] /= nl; n[2] /= nl; }
            else { n[0] = 0; n[1] = 1; n[2] = 0; }
        }
    }

    float color[3] = { 0.7f, 0.7f, 0.7f };
    if (defaultColor) {
        color[0] = defaultColor[0];
        color[1] = defaultColor[1];
        color[2] = defaultColor[2];
    }

    outVerts.clear();
    for (size_t i = 0; i < tempPositions.size() / 3; i++) {
        outVerts.push_back(tempPositions[i * 3 + 0]);
        outVerts.push_back(tempPositions[i * 3 + 1]);
        outVerts.push_back(tempPositions[i * 3 + 2]);
        outVerts.push_back(tempNormals[i * 3 + 0]);
        outVerts.push_back(tempNormals[i * 3 + 1]);
        outVerts.push_back(tempNormals[i * 3 + 2]);
        outVerts.push_back(color[0]);
        outVerts.push_back(color[1]);
        outVerts.push_back(color[2]);
    }

    outIndices.clear();
    for (auto& f : faces) {
        outIndices.push_back(f.i0);
        outIndices.push_back(f.i1);
        outIndices.push_back(f.i2);
    }

    return true;
}

bool LoadSdkMeshFile(const char* filename, SdkMeshResult& out, const float* defaultColor)
{
    std::vector<unsigned char> fileData;
    if (!ReadFileData(filename, fileData)) return false;

    std::vector<float> verts;
    std::vector<uint32_t> indices;
    if (!ParseSdkMesh(fileData.data(), fileData.size(), verts, indices, defaultColor))
        return false;

    out.Vertices = std::move(verts);
    out.Indices = std::move(indices);
    const int VERTEX_STRIDE = 9;
    out.VertexCount = (int)out.Vertices.size() / VERTEX_STRIDE;
    out.IndexCount = (int)out.Indices.size();
    out.Success = true;
    return true;
}

bool LoadSdkMeshAsObj(const char* filename, ObjMesh& out)
{
    SdkMeshResult result;
    if (!LoadSdkMeshFile(filename, result)) return false;

    out.vertices = std::move(result.Vertices);
    out.indices = std::move(result.Indices);
    out.vertCount = result.VertexCount;
    out.indexCount = result.IndexCount;
    return true;
}
