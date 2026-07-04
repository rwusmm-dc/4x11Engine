#include "Archive.h"
#include <cstring>
#include <algorithm>
#include <sys/stat.h>

extern "C" {
#include <zstd.h>
}

// ── helpers ──

static std::string AddSep(const std::string& dir) {
    if (dir.empty()) return "./";
    char c = dir.back();
    if (c != '/' && c != '\\') return dir + '/';
    return dir;
}

bool GameArchive::FileExists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

uint64_t GameArchive::FileSize(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (uint64_t)st.st_size;
}

// ── construction ──

GameArchive::GameArchive() {}
GameArchive::~GameArchive() {}

// ── mount ──

bool GameArchive::Mount(const char* exeDir) {
    std::string base = AddSep(exeDir);
    std::string pakPath = base + "game_data.pak";

    if (!FileExists(pakPath.c_str())) {
        printf("[PAK] game_data.pak not found at: %s\n", pakPath.c_str());
        return false;
    }

    m_PakPath = pakPath;

    FILE* f = fopen(pakPath.c_str(), "rb");
    if (!f) { printf("[PAK] fopen failed\n"); return false; }

    PAK2Header hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); printf("[PAK] header read failed\n"); return false; }
    if (memcmp(hdr.magic, "PAK2", 4) != 0 || hdr.version != 2) {
        printf("[PAK] bad magic/version: %c%c%c%c v%u\n",
               hdr.magic[0], hdr.magic[1], hdr.magic[2], hdr.magic[3], hdr.version);
        fclose(f); return false;
    }
    printf("[PAK] Mounted: %u entities, %u blocks, index at %llu\n",
           hdr.entityCount, hdr.blockCount, hdr.indexOffset);

    // Seek to index
    if (_fseeki64(f, hdr.indexOffset, SEEK_SET) != 0) { fclose(f); printf("[PAK] seek to index failed\n"); return false; }

    // Read block info
    m_Blocks.reserve(hdr.blockCount);
    for (uint32_t i = 0; i < hdr.blockCount; i++) {
        BlockInfo bi;
        if (fread(&bi, sizeof(bi), 1, f) != 1) { fclose(f); return false; }
        Block b;
        b.compressedSize   = bi.compressedSize;
        b.uncompressedSize = bi.uncompressedSize;
        b.fileOffset       = bi.fileOffset;
        b.entityCount      = bi.entityCount;
        m_Blocks.push_back(b);
        m_TotalSize += bi.uncompressedSize;
    }

    // Read entity index
    m_Index.resize(hdr.entityCount);
    if (hdr.entityCount > 0) {
        size_t read = fread(m_Index.data(), sizeof(EntityEntry), hdr.entityCount, f);
        if (read != hdr.entityCount) { fclose(f); m_Index.clear(); return false; }
    }

    fclose(f);
    return true;
}

// ── read + decompress a block ──

std::vector<uint8_t> GameArchive::ReadAndDecompress(const Block& block) {
    FILE* f = fopen(m_PakPath.c_str(), "rb");
    if (!f) return {};

    if (_fseeki64(f, block.fileOffset, SEEK_SET) != 0) { fclose(f); return {}; }

    uint64_t compSize = 0, uncompSize = 0;
    if (fread(&compSize, sizeof(compSize), 1, f) != 1) { fclose(f); return {}; }
    if (fread(&uncompSize, sizeof(uncompSize), 1, f) != 1) { fclose(f); return {}; }

    if (compSize != block.compressedSize || uncompSize != block.uncompressedSize) {
        fclose(f);
        return {};
    }

    std::vector<uint8_t> compressed(compSize);
    if (fread(compressed.data(), 1, compSize, f) != compSize) { fclose(f); return {}; }
    fclose(f);

    std::vector<uint8_t> decompressed(uncompSize);
    size_t result = ZSTD_decompress(decompressed.data(), uncompSize,
                                    compressed.data(), compSize);
    if (ZSTD_isError(result) || result != uncompSize) {
        return {};
    }

    return decompressed;
}

std::vector<uint8_t> GameArchive::GetBlockData(int blockIndex) {
    if (blockIndex < 0 || blockIndex >= (int)m_Blocks.size()) return {};
    return ReadAndDecompress(m_Blocks[blockIndex]);
}
