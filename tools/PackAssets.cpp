#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <sys/stat.h>
#include <io.h>

extern "C" {
#include <zstd.h>
}

const uint64_t SPLIT_THRESHOLD   = 100ULL * 1024 * 1024; // 100 MB
const uint64_t TARGET_CHUNK_SIZE = 500ULL * 1024 * 1024; // 500 MB
const int CHUNKS_FOLDER_THRESHOLD = 20;

struct IndexEntry {
    char name[64];
    uint8_t archiveIndex;
    uint64_t offset;
    uint32_t size;
};

#pragma pack(push, 1)
struct PAKHeader {
    char magic[4];
    uint32_t version;
    uint32_t entityCount;
    uint64_t compressedSize;
    uint64_t uncompressedSize;
};
#pragma pack(pop)

struct EntityFile {
    std::string name;
    std::string path;
    uint64_t size;
    std::vector<uint8_t> data;
};

static std::string FormatSize(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024ULL * 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.2f GB", (double)bytes / (1024.0*1024.0*1024.0));
    else if (bytes >= 1024ULL * 1024)
        snprintf(buf, sizeof(buf), "%.0f MB", (double)bytes / (1024.0*1024.0));
    else if (bytes >= 1024ULL)
        snprintf(buf, sizeof(buf), "%.0f KB", (double)bytes / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    return std::string(buf);
}

static bool ReadFile(const std::string& path, std::vector<uint8_t>& out) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    struct stat st;
    stat(path.c_str(), &st);
    out.resize(st.st_size);
    if (fread(out.data(), 1, st.st_size, f) != (size_t)st.st_size) {
        fclose(f);
        return false;
    }
    fclose(f);
    return true;
}

static char GetLetterForIndex(int idx) {
    return (char)('a' + idx);
}

int main(int argc, char* argv[]) {
    std::string inputDir;
    std::string outputDir;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--input") == 0 && i + 1 < argc)
            inputDir = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc)
            outputDir = argv[++i];
    }

    if (inputDir.empty() || outputDir.empty()) {
        printf("Usage: PackAssets.exe --input ./entities/ --output ./\n");
        return 1;
    }

    if (outputDir.back() != '/' && outputDir.back() != '\\')
        outputDir += '/';

    printf("[INFO] Scanning for game archives...\n");

    // Scan for .bin files in input directory
    std::vector<EntityFile> files;
    std::string searchPath = inputDir + "/*.bin";
    struct _finddata_t fd;
    intptr_t handle = _findfirst(searchPath.c_str(), &fd);
    if (handle != -1) {
        do {
            if (!(fd.attrib & _A_SUBDIR)) {
                EntityFile ef;
                ef.name = fd.name;
                // Strip extension for entity name
                size_t dot = ef.name.rfind('.');
                if (dot != std::string::npos) ef.name = ef.name.substr(0, dot);
                ef.path = inputDir + "/" + fd.name;
                ef.size = fd.size;
                files.push_back(ef);
            }
        } while (_findnext(handle, &fd) == 0);
        _findclose(handle);
    }

    if (files.empty()) {
        printf("[ERROR] No .bin entity files found in %s\n", inputDir.c_str());
        return 1;
    }

    // Calculate total uncompressed size
    uint64_t totalSize = 0;
    for (auto& f : files) totalSize += f.size;

    printf("[INFO] Total game size: %s", FormatSize(totalSize).c_str());

    // Smart decision: determine number of chunks
    int chunkCount = 1;
    if (totalSize >= SPLIT_THRESHOLD) {
        chunkCount = (int)std::ceil((double)totalSize / TARGET_CHUNK_SIZE);
        printf(" (above %s threshold)\n", FormatSize(SPLIT_THRESHOLD).c_str());
        printf("[INFO] Splitting into %d chunks (target: %s each)\n",
               chunkCount, FormatSize(TARGET_CHUNK_SIZE).c_str());
    } else {
        printf(" (below %s threshold)\n", FormatSize(SPLIT_THRESHOLD).c_str());
        printf("[INFO] Creating single archive: NO SPLIT\n");
    }

    std::string chunkDir = outputDir;
    bool useChunkFolder = (chunkCount > CHUNKS_FOLDER_THRESHOLD);
    if (useChunkFolder) {
        chunkDir = outputDir + "chunks/";
        mkdir(chunkDir.c_str());
        printf("[INFO] Using chunks/ folder for %d archive files\n", chunkCount);
    }

    // Assign files to chunks (greedy fill)
    std::vector<std::vector<size_t>> chunks(chunkCount);
    {
        uint64_t totalPerChunk = (totalSize + chunkCount - 1) / chunkCount;
        int ci = 0;
        uint64_t currentSize = 0;
        for (size_t fi = 0; fi < files.size(); fi++) {
            if (ci < chunkCount - 1 && currentSize + files[fi].size > totalPerChunk && currentSize > 0) {
                ci++;
                currentSize = 0;
            }
            chunks[ci].push_back(fi);
            currentSize += files[fi].size;
        }
    }

    // Build index
    std::vector<IndexEntry> index;
    index.reserve(files.size());

    // Pack each chunk
    for (int ci = 0; ci < chunkCount; ci++) {
        std::string pakName;
        if (chunkCount <= 26) {
            char letter = GetLetterForIndex(ci);
            pakName = (useChunkFolder ? chunkDir : outputDir) + "world_" + letter + ".pak";
        } else {
            char name[32];
            snprintf(name, sizeof(name), "world_%d.pak", ci);
            pakName = (useChunkFolder ? chunkDir : outputDir) + name;
        }

        // Concatenate entity data
        std::vector<uint8_t> uncompressed;
        for (size_t fi : chunks[ci]) {
            if (!ReadFile(files[fi].path, files[fi].data)) {
                printf("[ERROR] Failed to read %s\n", files[fi].path.c_str());
                return 1;
            }
            // Prepend entity count prefix for each entity? No - the data IS the entity serialization
            // Store offset and size for index
            IndexEntry ie;
            memset(ie.name, 0, sizeof(ie.name));
            strncpy(ie.name, files[fi].name.c_str(), sizeof(ie.name) - 1);
            ie.archiveIndex = (uint8_t)(chunkCount <= 26 ? ci : ci);
            ie.offset = uncompressed.size();
            ie.size = (uint32_t)files[fi].data.size();
            index.push_back(ie);

            uncompressed.insert(uncompressed.end(), files[fi].data.begin(), files[fi].data.end());
        }

        // Compress with ZSTD
        uint64_t uncompressedSize = uncompressed.size();
        size_t maxCompressed = ZSTD_compressBound(uncompressedSize);
        std::vector<uint8_t> compressed(maxCompressed);
        size_t compressedSize = ZSTD_compress(compressed.data(), maxCompressed,
                                              uncompressed.data(), uncompressedSize, 3);
        if (ZSTD_isError(compressedSize)) {
            printf("[ERROR] ZSTD compression failed: %s\n", ZSTD_getErrorName(compressedSize));
            return 1;
        }
        compressed.resize(compressedSize);

        // Write .pak file
        FILE* f = fopen(pakName.c_str(), "wb");
        if (!f) {
            printf("[ERROR] Could not write %s\n", pakName.c_str());
            return 1;
        }
        PAKHeader hdr;
        memcpy(hdr.magic, "PAK1", 4);
        hdr.version = 1;
        hdr.entityCount = (uint32_t)chunks[ci].size();
        hdr.compressedSize = compressedSize;
        hdr.uncompressedSize = uncompressedSize;
        fwrite(&hdr, sizeof(hdr), 1, f);
        fwrite(compressed.data(), 1, compressedSize, f);
        fclose(f);

        printf("[INFO] Created %s (%s compressed, %s uncompressed, %u entities)\n",
               pakName.c_str(),
               FormatSize(compressedSize).c_str(),
               FormatSize(uncompressedSize).c_str(),
               hdr.entityCount);
    }

    // Write world_index.dat
    std::string indexPath = outputDir + "world_index.dat";
    FILE* f = fopen(indexPath.c_str(), "wb");
    if (!f) {
        printf("[ERROR] Could not write %s\n", indexPath.c_str());
        return 1;
    }
    uint32_t indexCount = (uint32_t)index.size();
    fwrite(&indexCount, sizeof(indexCount), 1, f);
    fwrite(index.data(), sizeof(IndexEntry), indexCount, f);
    fclose(f);

    printf("[INFO] Wrote %s with %u entries\n", indexPath.c_str(), indexCount);
    printf("[INFO] Pack complete: %d chunk%s, %u entities, %s total\n",
           chunkCount, chunkCount == 1 ? "" : "s",
           indexCount,
           FormatSize(totalSize).c_str());

    return 0;
}
