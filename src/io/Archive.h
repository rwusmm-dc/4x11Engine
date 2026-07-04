#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>

#pragma pack(push, 1)
struct PAK2Header {
    char magic[4];          // "PAK2"
    uint32_t version;       // 2
    uint32_t entityCount;
    uint32_t blockCount;
    uint64_t indexOffset;   // byte offset from file start to index section
};

struct BlockInfo {
    uint64_t compressedSize;
    uint64_t uncompressedSize;
    uint64_t fileOffset;    // byte offset from file start to compressedSize field
    uint32_t entityCount;
};

struct EntityEntry {
    char name[64];
    uint32_t blockIndex;
    uint32_t offsetInBlock; // offset in decompressed block data
    uint32_t size;          // size in decompressed block data
};
#pragma pack(pop)

class GameArchive {
public:
    GameArchive();
    ~GameArchive();

    bool Mount(const char* exeDir);
    int GetBlockCount() const { return (int)m_Blocks.size(); }
    uint64_t GetTotalSize() const { return m_TotalSize; }
    const std::vector<EntityEntry>& GetIndex() const { return m_Index; }

    std::vector<uint8_t> GetBlockData(int blockIndex);

private:
    struct Block {
        uint64_t compressedSize;
        uint64_t uncompressedSize;
        uint64_t fileOffset;
        uint32_t entityCount;
    };

    std::vector<Block> m_Blocks;
    std::vector<EntityEntry> m_Index;
    uint64_t m_TotalSize = 0;
    std::string m_PakPath;

    static bool FileExists(const char* path);
    static uint64_t FileSize(const char* path);
    std::vector<uint8_t> ReadAndDecompress(const Block& block);
};
