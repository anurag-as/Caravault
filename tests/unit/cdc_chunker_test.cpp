#include "cdc_chunker.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <random>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

fs::path write_temp_file(const std::vector<uint8_t>& data, const std::string& name) {
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p, std::ios::binary);
    if (!data.empty())
        f.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size()));
    return p;
}

std::vector<uint8_t> make_random_bytes(size_t n, uint32_t seed = 42) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<unsigned> dist(0, 255);
    std::vector<uint8_t> buf(n);
    for (auto& b : buf)
        b = static_cast<uint8_t>(dist(rng));
    return buf;
}

std::vector<uint8_t> make_large_random(size_t n = 2 * 1024 * 1024, uint32_t seed = 7) {
    return make_random_bytes(n, seed);
}

}  // namespace

// Construction

TEST(CDCChunkerTest, DefaultConstructionSucceeds) {
    EXPECT_NO_THROW(CDCChunker());
}

TEST(CDCChunkerTest, CustomAvgChunkSizeSucceeds) {
    EXPECT_NO_THROW(CDCChunker(32 * 1024));
    EXPECT_NO_THROW(CDCChunker(64 * 1024));
    EXPECT_NO_THROW(CDCChunker(128 * 1024));
}

TEST(CDCChunkerTest, TooSmallAvgChunkSizeThrows) {
    EXPECT_THROW(CDCChunker(CDCChunker::kMinChunkSize - 1), std::invalid_argument);
}

TEST(CDCChunkerTest, TooLargeAvgChunkSizeThrows) {
    EXPECT_THROW(CDCChunker(CDCChunker::kMaxChunkSize + 1), std::invalid_argument);
}

// Small files (<=1 MB) → single chunk

TEST(CDCChunkerTest, EmptyFileProducesSingleChunk) {
    auto path = write_temp_file({}, "cdc_empty.bin");
    CDCChunker chunker;
    auto chunks = chunker.chunk_file(path);
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0].offset, 0u);
    EXPECT_EQ(chunks[0].size, 0u);
    fs::remove(path);
}

TEST(CDCChunkerTest, SmallFileProducesSingleChunk) {
    auto data = make_random_bytes(4096);
    auto path = write_temp_file(data, "cdc_small.bin");
    CDCChunker chunker;
    auto chunks = chunker.chunk_file(path);
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_EQ(chunks[0].offset, 0u);
    EXPECT_EQ(chunks[0].size, 4096u);
    EXPECT_FALSE(chunks[0].hash.empty());
    fs::remove(path);
}

TEST(CDCChunkerTest, ExactThresholdFileProducesSingleChunk) {
    auto data = make_random_bytes(CDCChunker::kLargeFileThreshold);
    auto path = write_temp_file(data, "cdc_threshold.bin");
    CDCChunker chunker;
    auto chunks = chunker.chunk_file(path);
    ASSERT_EQ(chunks.size(), 1u);
    fs::remove(path);
}

// Content-defined: same content → same chunks

TEST(CDCChunkerTest, SameContentProducesSameChunks) {
    auto data = make_large_random();
    auto path1 = write_temp_file(data, "cdc_same1.bin");
    auto path2 = write_temp_file(data, "cdc_same2.bin");

    CDCChunker chunker;
    auto chunks1 = chunker.chunk_file(path1);
    auto chunks2 = chunker.chunk_file(path2);

    ASSERT_EQ(chunks1.size(), chunks2.size());
    for (size_t i = 0; i < chunks1.size(); ++i) {
        EXPECT_EQ(chunks1[i].offset, chunks2[i].offset);
        EXPECT_EQ(chunks1[i].size, chunks2[i].size);
        EXPECT_EQ(chunks1[i].hash, chunks2[i].hash);
    }

    fs::remove(path1);
    fs::remove(path2);
}

// Chunk size constraints

TEST(CDCChunkerTest, AllChunksSatisfySizeConstraints) {
    auto data = make_large_random(4 * 1024 * 1024);
    auto path = write_temp_file(data, "cdc_size_check.bin");

    CDCChunker chunker;
    auto chunks = chunker.chunk_file(path);

    ASSERT_GT(chunks.size(), 1u);
    for (size_t i = 0; i + 1 < chunks.size(); ++i) {
        EXPECT_GE(chunks[i].size, CDCChunker::kMinChunkSize)
            << "Chunk " << i << " is below minimum size";
        EXPECT_LE(chunks[i].size, CDCChunker::kMaxChunkSize)
            << "Chunk " << i << " exceeds maximum size";
    }
    EXPECT_LE(chunks.back().size, CDCChunker::kMaxChunkSize);

    fs::remove(path);
}

TEST(CDCChunkerTest, ChunksCoverEntireFile) {
    auto data = make_large_random(3 * 1024 * 1024);
    auto path = write_temp_file(data, "cdc_coverage.bin");

    CDCChunker chunker;
    auto chunks = chunker.chunk_file(path);

    uint64_t expected_offset = 0;
    uint64_t total_size = 0;
    for (const auto& c : chunks) {
        EXPECT_EQ(c.offset, expected_offset) << "Gap or overlap in chunk offsets";
        expected_offset += c.size;
        total_size += c.size;
    }
    EXPECT_EQ(total_size, data.size());

    fs::remove(path);
}

// Chunk hashes are 64 hex chars (SHA-256)

TEST(CDCChunkerTest, ChunkHashesAreSHA256) {
    auto data = make_large_random();
    auto path = write_temp_file(data, "cdc_hash_check.bin");

    CDCChunker chunker;
    auto chunks = chunker.chunk_file(path);

    for (const auto& c : chunks) {
        EXPECT_EQ(c.hash.size(), 64u);
        for (char ch : c.hash)
            EXPECT_TRUE(std::isxdigit(static_cast<unsigned char>(ch)));
    }

    fs::remove(path);
}

// diff_chunks

TEST(CDCChunkerTest, DiffChunksEmptyOldReturnsAllNew) {
    std::vector<Chunk> old_chunks;
    std::vector<Chunk> new_chunks = {{0, 1024, "aabbcc"}, {1024, 2048, "ddeeff"}};
    auto diff = CDCChunker::diff_chunks(old_chunks, new_chunks);
    EXPECT_EQ(diff.size(), new_chunks.size());
}

TEST(CDCChunkerTest, DiffChunksIdenticalReturnsEmpty) {
    std::vector<Chunk> chunks = {{0, 1024, "aabbcc"}, {1024, 2048, "ddeeff"}};
    auto diff = CDCChunker::diff_chunks(chunks, chunks);
    EXPECT_TRUE(diff.empty());
}

TEST(CDCChunkerTest, DiffChunksReturnsOnlyChangedChunks) {
    std::vector<Chunk> old_chunks = {
        {0, 1024, "hash_a"}, {1024, 1024, "hash_b"}, {2048, 1024, "hash_c"}};
    std::vector<Chunk> new_chunks = {
        {0, 1024, "hash_a"}, {1024, 1024, "hash_b_modified"}, {2048, 1024, "hash_c"}};
    auto diff = CDCChunker::diff_chunks(old_chunks, new_chunks);
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_EQ(diff[0].hash, "hash_b_modified");
}

TEST(CDCChunkerTest, DiffChunksAllNewReturnsAll) {
    std::vector<Chunk> old_chunks = {{0, 1024, "old_a"}, {1024, 1024, "old_b"}};
    std::vector<Chunk> new_chunks = {{0, 1024, "new_a"}, {1024, 1024, "new_b"}};
    auto diff = CDCChunker::diff_chunks(old_chunks, new_chunks);
    EXPECT_EQ(diff.size(), 2u);
}

TEST(CDCChunkerTest, DiffChunksRealFiles) {
    auto data = make_large_random(2 * 1024 * 1024, 99);
    auto path_v1 = write_temp_file(data, "cdc_diff_v1.bin");

    auto data_v2 = data;
    size_t mid = data_v2.size() / 2;
    for (size_t i = mid; i < mid + 4096 && i < data_v2.size(); ++i)
        data_v2[i] ^= 0xFF;
    auto path_v2 = write_temp_file(data_v2, "cdc_diff_v2.bin");

    CDCChunker chunker;
    auto chunks_v1 = chunker.chunk_file(path_v1);
    auto chunks_v2 = chunker.chunk_file(path_v2);
    auto diff = CDCChunker::diff_chunks(chunks_v1, chunks_v2);

    EXPECT_GT(diff.size(), 0u);
    EXPECT_LT(diff.size(), chunks_v2.size());

    fs::remove(path_v1);
    fs::remove(path_v2);
}
