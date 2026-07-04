#pragma once
#include <vector>
#include <cstdint>

struct ObjMesh {
    std::vector<float>    vertices;
    std::vector<uint32_t> indices;
    int vertCount  = 0;
    int indexCount = 0;
};

bool LoadObj(const char* filename, ObjMesh& out);