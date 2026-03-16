#ifndef CARAVAULT_MERKLE_ENGINE_HPP
#define CARAVAULT_MERKLE_ENGINE_HPP

#include "manifest_store.hpp"

#include <string>
#include <vector>

namespace caravault {

/**
 * A node in the Merkle tree.
 *
 * level == 0: leaf (file)
 * level == 1: directory
 */
struct MerkleNode {
    std::string path;                  // Relative path from drive root (forward slashes)
    std::string hash;                  // SHA-256 hash
    int level = 0;                     // 0 = file, 1 = directory
    std::vector<MerkleNode> children;  // Non-empty only for directories
};

/**
 * MerkleEngine builds and diffs Merkle trees over a filesystem directory,
 * using ManifestStore for hash caching.
 */
class MerkleEngine {
public:
    /**
     * Compute SHA-256 of a file's contents using the OpenSSL EVP API.
     * Reads the file in 64 KB chunks.
     *
     * Returns a lowercase hex-encoded hash string.
     * Throws std::runtime_error on I/O or OpenSSL failure.
     */
    static std::string compute_file_hash(const fs::path& file_path);

    /**
     * Compute SHA-256 of sorted, concatenated child hashes.
     * Returns a lowercase hex-encoded hash string.
     */
    static std::string compute_directory_hash(const std::vector<std::string>& child_hashes);

    /**
     * Recursively scan root_path and build a Merkle tree.
     *
     * Uses ManifestStore to cache file hashes: if a file's stored mtime and
     * size match the on-disk values the cached hash is reused without
     * recomputation. Computed hashes are written back to the store.
     */
    static MerkleNode build_tree(const fs::path& root_path, ManifestStore& store);

    /**
     * Files that differ between two trees.
     * Only leaf nodes (level == 0) appear in these lists.
     */
    struct Diff {
        std::vector<std::string> added;
        std::vector<std::string> modified;
        std::vector<std::string> deleted;
    };

    /**
     * Compare two Merkle trees and return the set of changed files.
     * Subtrees with matching root hashes are skipped entirely.
     */
    static Diff diff(const MerkleNode& tree1, const MerkleNode& tree2);

private:
    static MerkleNode build_node(const fs::path& root_path,
                                 const fs::path& current_path,
                                 ManifestStore& store);

    static void collect_leaves(const MerkleNode& node, std::vector<std::string>& out);
};

}  // namespace caravault

#endif  // CARAVAULT_MERKLE_ENGINE_HPP
