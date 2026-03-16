#include "cdc_chunker.hpp"

#include <fstream>
#include <iomanip>
#include <mutex>
#include <openssl/evp.h>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace caravault {

namespace {

// Irreducible polynomial over GF(2^64) for Rabin fingerprinting.
constexpr uint64_t kRabinPoly = 0xbfe6b8a5bf378d83ULL;

const uint64_t* rabin_table() {
    static uint64_t table[256];
    static std::once_flag flag;
    std::call_once(flag, [] {
        for (int i = 0; i < 256; ++i) {
            uint64_t v = static_cast<uint64_t>(i);
            for (int bit = 0; bit < 8; ++bit)
                v = (v & 1ULL) ? (v >> 1) ^ kRabinPoly : v >> 1;
            table[i] = v;
        }
    });
    return table;
}

// Returns the smallest power-of-two >= avg, minus one, as a bit mask.
uint64_t mask_for_avg(size_t avg) {
    uint64_t p = 1;
    while (p < static_cast<uint64_t>(avg))
        p <<= 1;
    return p - 1;
}

}  // namespace

CDCChunker::CDCChunker(size_t avg_chunk_size) : avg_chunk_size_(avg_chunk_size) {
    if (avg_chunk_size < kMinChunkSize || avg_chunk_size > kMaxChunkSize)
        throw std::invalid_argument(
            "avg_chunk_size must be between kMinChunkSize (16 KB) and kMaxChunkSize (256 KB)");
    mask_ = mask_for_avg(avg_chunk_size_);
}

std::string CDCChunker::hash_buffer(const uint8_t* data, size_t len) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        throw std::runtime_error("EVP_MD_CTX_new failed");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }
    if (len > 0 && EVP_DigestUpdate(ctx, data, len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestUpdate failed");
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digest_len; ++i)
        oss << std::setw(2) << static_cast<unsigned>(digest[i]);
    return oss.str();
}

std::vector<Chunk> CDCChunker::chunk_buffer(const uint8_t* data,
                                            size_t len,
                                            uint64_t base_offset) const {
    const uint64_t* table = rabin_table();
    std::vector<Chunk> chunks;

    size_t chunk_start = 0;
    uint64_t fp = 0;

    for (size_t i = 0; i < len; ++i) {
        fp = (fp >> 8) ^ table[(fp ^ data[i]) & 0xFF];

        size_t chunk_len = i - chunk_start + 1;
        bool at_boundary = chunk_len >= kMinChunkSize && ((fp & mask_) == 0);
        bool at_max = chunk_len >= kMaxChunkSize;

        if (at_boundary || at_max) {
            chunks.push_back(
                {base_offset + chunk_start, chunk_len, hash_buffer(data + chunk_start, chunk_len)});
            chunk_start = i + 1;
            fp = 0;
        }
    }

    if (chunk_start < len) {
        size_t tail = len - chunk_start;
        chunks.push_back({base_offset + chunk_start, tail, hash_buffer(data + chunk_start, tail)});
    }

    return chunks;
}

std::vector<Chunk> CDCChunker::chunk_file(const fs::path& file_path) const {
    std::ifstream file(file_path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("Cannot open file: " + file_path.string());

    auto file_size = static_cast<uint64_t>(file.tellg());
    file.seekg(0);

    if (file_size <= kLargeFileThreshold) {
        std::vector<uint8_t> buf(static_cast<size_t>(file_size));
        if (file_size > 0 && !file.read(reinterpret_cast<char*>(buf.data()),
                                        static_cast<std::streamsize>(file_size)))
            throw std::runtime_error("Read error: " + file_path.string());
        return {{0, file_size, hash_buffer(buf.data(), static_cast<size_t>(file_size))}};
    }

    std::vector<uint8_t> buf(static_cast<size_t>(file_size));
    if (!file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(file_size)))
        throw std::runtime_error("Read error: " + file_path.string());

    return chunk_buffer(buf.data(), buf.size(), 0);
}

std::vector<Chunk> CDCChunker::diff_chunks(const std::vector<Chunk>& old_chunks,
                                           const std::vector<Chunk>& new_chunks) {
    std::unordered_set<std::string> old_hashes;
    old_hashes.reserve(old_chunks.size());
    for (const auto& c : old_chunks)
        old_hashes.insert(c.hash);

    std::vector<Chunk> changed;
    for (const auto& c : new_chunks) {
        if (old_hashes.find(c.hash) == old_hashes.end())
            changed.push_back(c);
    }
    return changed;
}

}  // namespace caravault
