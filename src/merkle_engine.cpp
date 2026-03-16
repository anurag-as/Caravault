#include "merkle_engine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <map>
#include <openssl/evp.h>
#include <sstream>
#include <stdexcept>

namespace caravault {

namespace {

std::string hex_encode(const unsigned char* data, unsigned int len) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    return oss.str();
}

std::string sha256_bytes(const unsigned char* data, size_t len) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        throw std::runtime_error("EVP_MD_CTX_new failed");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }
    if (EVP_DigestUpdate(ctx, data, len) != 1) {
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
    return hex_encode(digest, digest_len);
}

}  // namespace

std::string MerkleEngine::compute_file_hash(const fs::path& file_path) {
    constexpr size_t CHUNK_SIZE = 64 * 1024;

    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + file_path.string());
    }

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx)
        throw std::runtime_error("EVP_MD_CTX_new failed");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }

    std::array<char, 64 * 1024> buf{};
    while (file.read(buf.data(), CHUNK_SIZE) || file.gcount() > 0) {
        auto n = static_cast<size_t>(file.gcount());
        if (EVP_DigestUpdate(ctx, buf.data(), n) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("EVP_DigestUpdate failed");
        }
    }

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }

    EVP_MD_CTX_free(ctx);
    return hex_encode(digest, digest_len);
}

std::string MerkleEngine::compute_directory_hash(const std::vector<std::string>& child_hashes) {
    std::vector<std::string> sorted = child_hashes;
    std::sort(sorted.begin(), sorted.end());

    std::string concatenated;
    for (const auto& h : sorted) {
        concatenated += h;
    }

    return sha256_bytes(reinterpret_cast<const unsigned char*>(concatenated.data()),
                        concatenated.size());
}

MerkleNode MerkleEngine::build_node(const fs::path& root_path,
                                    const fs::path& current_path,
                                    ManifestStore& store) {
    MerkleNode node;
    node.path = fs::relative(current_path, root_path).generic_string();

    if (fs::is_regular_file(current_path)) {
        node.level = 0;

        auto cached_meta = store.get_file(node.path);
        if (cached_meta.has_value()) {
            auto file_size = fs::file_size(current_path);
            auto mtime =
                static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                          fs::last_write_time(current_path).time_since_epoch())
                                          .count());

            if (cached_meta->size == file_size && cached_meta->mtime == mtime) {
                auto cached_hash = store.get_merkle_hash(node.path, 0);
                if (cached_hash.has_value()) {
                    node.hash = *cached_hash;
                    return node;
                }
            }
        }

        node.hash = compute_file_hash(current_path);
        store.upsert_merkle_node(node.path, node.hash, 0);

    } else if (fs::is_directory(current_path)) {
        node.level = 1;

        std::vector<fs::path> entries;
        for (const auto& entry : fs::directory_iterator(current_path)) {
            entries.push_back(entry.path());
        }
        std::sort(entries.begin(), entries.end());

        std::vector<std::string> child_hashes;
        for (const auto& entry : entries) {
            if (fs::is_regular_file(entry) || fs::is_directory(entry)) {
                MerkleNode child = build_node(root_path, entry, store);
                child_hashes.push_back(child.hash);
                node.children.push_back(std::move(child));
            }
        }

        node.hash = compute_directory_hash(child_hashes);
        store.upsert_merkle_node(node.path, node.hash, 1);
    }

    return node;
}

MerkleNode MerkleEngine::build_tree(const fs::path& root_path, ManifestStore& store) {
    return build_node(root_path, root_path, store);
}

void MerkleEngine::collect_leaves(const MerkleNode& node, std::vector<std::string>& out) {
    if (node.level == 0) {
        out.push_back(node.path);
    } else {
        for (const auto& child : node.children) {
            collect_leaves(child, out);
        }
    }
}

MerkleEngine::Diff MerkleEngine::diff(const MerkleNode& tree1, const MerkleNode& tree2) {
    Diff result;

    if (tree1.hash == tree2.hash) {
        return result;
    }

    if (tree1.level == 0 && tree2.level == 0) {
        result.modified.push_back(tree1.path);
        return result;
    }

    std::map<std::string, const MerkleNode*> map1, map2;
    for (const auto& child : tree1.children) {
        map1[child.path] = &child;
    }
    for (const auto& child : tree2.children) {
        map2[child.path] = &child;
    }

    for (const auto& [path, node] : map1) {
        if (map2.find(path) == map2.end()) {
            collect_leaves(*node, result.deleted);
        }
    }

    for (const auto& [path, node] : map2) {
        if (map1.find(path) == map1.end()) {
            collect_leaves(*node, result.added);
        }
    }

    for (const auto& [path, node2] : map2) {
        auto it = map1.find(path);
        if (it == map1.end())
            continue;
        const MerkleNode* node1 = it->second;

        if (node1->hash == node2->hash)
            continue;

        auto sub = diff(*node1, *node2);
        result.added.insert(result.added.end(), sub.added.begin(), sub.added.end());
        result.modified.insert(result.modified.end(), sub.modified.begin(), sub.modified.end());
        result.deleted.insert(result.deleted.end(), sub.deleted.begin(), sub.deleted.end());
    }

    return result;
}

}  // namespace caravault
