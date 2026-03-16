#ifndef CARAVAULT_CDC_CHUNKER_HPP
#define CARAVAULT_CDC_CHUNKER_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace caravault {

namespace fs = std::filesystem;

/**
 * A content-defined chunk produced by CDCChunker.
 */
struct Chunk {
    uint64_t offset;
    uint64_t size;
    std::string hash;  // SHA-256 hex digest of chunk contents
};

/**
 * Content-Defined Chunking using a Rabin fingerprint rolling hash.
 *
 * Splits files into variable-size chunks whose boundaries are determined by
 * file content rather than fixed offsets.  Identical content regions produce
 * identical chunks even after insertions or deletions elsewhere in the file,
 * enabling efficient delta synchronization.
 *
 * Files <= kLargeFileThreshold are returned as a single chunk without CDC.
 */
class CDCChunker {
public:
    static constexpr size_t kMinChunkSize = 16 * 1024;          // 16 KB
    static constexpr size_t kMaxChunkSize = 256 * 1024;         // 256 KB
    static constexpr size_t kLargeFileThreshold = 1024 * 1024;  // 1 MB

    /**
     * Construct a chunker with the given average chunk size.
     * avg_chunk_size must be in [kMinChunkSize, kMaxChunkSize].
     * Throws std::invalid_argument if out of range.
     */
    explicit CDCChunker(size_t avg_chunk_size = 64 * 1024);

    /**
     * Split a file into content-defined chunks.
     * Throws std::runtime_error on I/O failure.
     */
    std::vector<Chunk> chunk_file(const fs::path& file_path) const;

    /**
     * Return the chunks from new_chunks whose hash is absent in old_chunks.
     * Only changed chunks need to be transferred during synchronization.
     */
    static std::vector<Chunk> diff_chunks(const std::vector<Chunk>& old_chunks,
                                          const std::vector<Chunk>& new_chunks);

private:
    size_t avg_chunk_size_;
    uint64_t mask_;

    static std::string hash_buffer(const uint8_t* data, size_t len);
    std::vector<Chunk> chunk_buffer(const uint8_t* data, size_t len, uint64_t base_offset) const;
};

}  // namespace caravault

#endif  // CARAVAULT_CDC_CHUNKER_HPP
