#pragma once

#include <vector>
#include <cstdint>

#pragma pack(push, 1)

struct SdkMeshHeader {
    char     Magic[8];
    char     Version[8];
    uint64_t HeaderSize;
    uint64_t NonBufferDataSize;
    uint64_t BufferDataSize;
    uint32_t NumVertexBuffers;
    uint32_t NumIndexBuffers;
    uint32_t NumMeshes;
    uint32_t NumSubsets;
    uint32_t NumFrameInfluences;
    uint32_t NumMaterials;
    uint64_t VertexStreamHeadersOffset;
    uint64_t IndexStreamHeadersOffset;
    uint64_t MeshDataOffset;
    uint64_t SubsetDataOffset;
    uint64_t FrameDataOffset;
    uint64_t MaterialDataOffset;
};

struct SdkMeshVertexBufferHeader {
    uint64_t NumVertices;
    uint64_t StrideBytes;
    uint64_t SizeBytes;
    uint64_t DataOffset;
};

struct SdkMeshIndexBufferHeader {
    uint64_t NumIndices;
    uint64_t SizeBytes;
    uint64_t DataOffset;
    uint32_t IndexType; // 0 = 16-bit, 1 = 32-bit
};

struct SdkMeshMesh {
    char     Name[100];
    uint8_t  NumVertexBuffers;
    uint32_t VertexBuffers[16];
    uint32_t IndexBuffer;
    uint32_t NumSubsets;
    uint32_t NumFrameInfluences;
    float    BoundingBoxCenter[3];
    float    BoundingBoxExtents[3];
    uint64_t SubsetOffset;
    uint64_t FrameInfluenceOffset;
};

struct SdkMeshSubset {
    char     Name[100];
    uint32_t MaterialID;
    uint32_t PrimitiveType;
    uint64_t IndexStart;
    uint64_t IndexCount;
    uint64_t VertexStart;
    uint64_t VertexCount;
};

struct SdkMeshMaterial {
    uint32_t DiffuseColor[4];
    uint32_t AmbientColor[4];
    uint32_t SpecularColor[4];
    uint32_t EmissiveColor[4];
    float    SpecularPower;
    uint32_t DiffuseTextureOffset;
    uint32_t NormalTextureOffset;
    uint32_t SpecularTextureOffset;
    uint32_t EmissiveTextureOffset;
    uint32_t MaterialNameOffset;
};

struct D3DDeclElement {
    uint16_t Stream;
    uint16_t Offset;
    uint8_t  Type;
    uint8_t  Method;
    uint8_t  Usage;
    uint8_t  UsageIndex;
};

#pragma pack(pop)

struct SdkMeshResult {
    std::vector<float>    Vertices;
    std::vector<uint32_t> Indices;
    int                   VertexCount = 0;
    int                   IndexCount = 0;
    bool                  Success = false;
};

struct ObjMesh;

bool LoadSdkMeshFile(const char* filename, SdkMeshResult& out,
                     const float defaultColor[3] = nullptr);
bool LoadSdkMeshAsObj(const char* filename, ObjMesh& out);
