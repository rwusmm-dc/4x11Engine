/**
 * @file SdkMeshLoader.h
 * @brief Microsoft DirectX SDK Mesh (.sdkmesh) format loader
 * 
 * Supports binary mesh files from the DirectX SDK Mesh Converter tool.
 * Handles version 1.00 and 1.01 formats with automatic index type detection.
 */

#pragma once

#include <vector>
#include <cstdint>
#include <string>

#pragma pack(push, 1)

struct SdkMeshHeader
{
    char     Magic[8];           // "SDKMESH\0"
    char     Version[8];         // "1.00\0\0\0\0" or "1.01\0\0\0\0"
    uint32_t NumMeshes;
    uint32_t NumMaterials;
    uint32_t NumVertices;
    uint32_t NumIndices;
    uint32_t NumAttributes;
    uint32_t NumVertexBufferDecls;
    uint32_t NumTextureCoords;
    uint32_t NumBones;
    uint32_t HeaderSize;
    uint32_t MeshOffset;
    uint32_t MaterialsOffset;
    uint32_t VerticesOffset;
    uint32_t IndicesOffset;
    uint32_t AttributesOffset;
    uint32_t VertexDeclOffset;
    uint32_t TextureCoordsOffset;
    uint32_t BonesOffset;
    uint32_t BoneIndicesOffset;
    uint32_t BoneNamesOffset;
};

struct SdkMesh
{
    uint32_t NumVertices;
    uint32_t NumIndices;
    uint32_t NumAttributes;
    uint32_t MaterialIndex;
    uint32_t VertexOffset;
    uint32_t IndexOffset;
    uint32_t AttributeOffset;
    uint32_t MaterialNameOffset;
    uint32_t MeshNameOffset;
};

struct SdkMaterial
{
    uint32_t DiffuseColor[4];       // ARGB
    uint32_t AmbientColor[4];       // ARGB
    uint32_t SpecularColor[4];      // ARGB
    uint32_t EmissiveColor[4];      // ARGB
    float    SpecularPower;
    uint32_t DiffuseTextureOffset;
    uint32_t NormalTextureOffset;
    uint32_t SpecularTextureOffset;
    uint32_t EmissiveTextureOffset;
    uint32_t MaterialNameOffset;
};

#pragma pack(pop)

struct SdkMeshResult
{
    std::vector<float>    Vertices;      // Interleaved: pos3 + normal3 + color3
    std::vector<uint32_t> Indices;       // Triangle indices
    int                   VertexCount;
    int                   IndexCount;
    bool                  Success;
    
    SdkMeshResult() : VertexCount(0), IndexCount(0), Success(false) {}
};

struct ObjMesh;

bool LoadSdkMeshFile(const char* filename, SdkMeshResult& out, 
                     const float defaultColor[3] = nullptr);

bool LoadSdkMeshAsObj(const char* filename, ObjMesh& out);