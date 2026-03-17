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
                                    ManifestStore& store,
                                    std::vector<ScanError>* errors) {
    MerkleNode node;
    std::error_code ec;
    node.path = fs::relative(current_path, root_path, ec).generic_string();
    if (ec)
        node.path = current_path.generic_string();

    if (fs::is_regular_file(current_path, ec) && !ec) {
        node.level = 0;

        auto file_size = fs::file_size(current_path, ec);
        if (ec) {
            if (errors)
                errors->push_back({node.path, "file_size: " + ec.message()});
            node.hash = "";
            return node;
        }

        auto mtime_tp = fs::last_write_time(current_path, ec);
        if (ec) {
            if (errors)
                errors->push_back({node.path, "last_write_time: " + ec.message()});
            node.hash = "";
            return node;
        }
        auto mtime = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(mtime_tp.time_since_epoch()).count());

        auto cached = store.get_file(node.path);
        if (cached.has_value() && !cached->hash.empty() && cached->size == file_size &&
            cached->mtime == mtime) {
            node.hash = cached->hash;
            return node;
        }

        try {
            node.hash = compute_file_hash(current_path);
        } catch (const std::exception& e) {
            if (errors)
                errors->push_back({node.path, std::string("hash: ") + e.what()});
            node.hash = "";
            return node;
        }

        try {
            store.upsert_merkle_node(node.path, node.hash, 0);
            FileMetadata meta = cached.value_or(FileMetadata{});
            meta.path = node.path;
            meta.hash = node.hash;
            meta.size = file_size;
            meta.mtime = mtime;
            store.upsert_file(meta);
        } catch (const std::exception& e) {
            if (errors)
                errors->push_back({node.path, std::string("db_write: ") + e.what()});
        }

    } else if (fs::is_directory(current_path, ec) && !ec) {
        node.level = 1;

        std::vector<fs::path> entries;
        try {
            for (const auto& entry : fs::directory_iterator(current_path)) {
                entries.push_back(entry.path());
            }
        } catch (const std::exception& e) {
            if (errors)
                errors->push_back({node.path, std::string("dir_iter: ") + e.what()});
            node.hash = compute_directory_hash({});
            return node;
        }
        std::sort(entries.begin(), entries.end());

        std::vector<std::string> child_hashes;
        for (const auto& entry : entries) {
            std::error_code entry_ec;
            bool is_file = fs::is_regular_file(entry, entry_ec);
            bool is_dir = !entry_ec && !is_file && fs::is_directory(entry, entry_ec);
            if (!entry_ec && (is_file || is_dir)) {
                MerkleNode child = build_node(root_path, entry, store, errors);
                if (!child.hash.empty()) {
                    child_hashes.push_back(child.hash);
                    node.children.push_back(std::move(child));
                }
            }
        }

        node.hash = compute_directory_hash(child_hashes);
        try {
            store.upsert_merkle_node(node.path, node.hash, 1);
        } catch (const std::exception& e) {
            if (errors)
                errors->push_back({node.path, std::string("db_write: ") + e.what()});
        }
    }

    return node;
}

MerkleNode MerkleEngine::build_tree(const fs::path& root_path, ManifestStore& store) {
    return build_node(root_path, root_path, store, nullptr);
}

MerkleNode MerkleEngine::build_tree(const fs::path& root_path,
                                    ManifestStore& store,
                                    std::vector<ScanError>& errors) {
    return build_node(root_path, root_path, store, &errors);
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
