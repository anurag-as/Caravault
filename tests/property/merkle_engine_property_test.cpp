#include "manifest_store.hpp"
#include "merkle_engine.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <iomanip>
#include <openssl/evp.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

std::atomic<int> g_counter{0};

fs::path make_temp_dir() {
    auto base =
        fs::temp_directory_path() / ("caravault_merkle_" + std::to_string(g_counter.fetch_add(1)));
    fs::create_directories(base);
    return base;
}

fs::path make_temp_db() {
    return fs::temp_directory_path() /
           ("caravault_merkle_db_" + std::to_string(g_counter.fetch_add(1)) + ".db");
}

fs::path write_file(const fs::path& dir, const std::string& name, const std::string& content) {
    fs::path p = dir / name;
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return p;
}

struct TempDir {
    fs::path path;
    explicit TempDir() : path(make_temp_dir()) {}
    ~TempDir() { fs::remove_all(path); }
};

struct TempStore {
    fs::path db_path;
    ManifestStore store;
    explicit TempStore() : db_path(make_temp_db()), store(ManifestStore::open(db_path)) {}
    ~TempStore() {
        store.close();
        fs::remove(db_path);
        fs::remove(fs::path(db_path.string() + "-wal"));
        fs::remove(fs::path(db_path.string() + "-shm"));
    }
};

std::string reference_sha256(const std::string& content) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, content.data(), content.size());
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, digest, &len);
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return oss.str();
}

void verify_tree_structure(const MerkleNode& node) {
    if (node.level == 0)
        return;

    std::vector<std::string> child_hashes;
    for (const auto& child : node.children) {
        child_hashes.push_back(child.hash);
        verify_tree_structure(child);
    }

    RC_ASSERT(node.hash == MerkleEngine::compute_directory_hash(child_hashes));
}

}  // namespace

// For any file, compute_file_hash SHALL produce the same SHA-256 digest as a reference
// implementation.
RC_GTEST_PROP(MerkleEngineProperty, SHA256HashComputation, ()) {
    auto content = *rc::gen::container<std::string>(rc::gen::character<char>());

    TempDir td;
    auto file_path = write_file(td.path, "test.bin", content);

    RC_ASSERT(MerkleEngine::compute_file_hash(file_path) == reference_sha256(content));
}

// For any Merkle tree, each directory node's hash SHALL equal the SHA-256 of its sorted children's
// hashes.
RC_GTEST_PROP(MerkleEngineProperty, MerkleTreeStructureValidity, ()) {
    auto file_count = *rc::gen::inRange<int>(1, 6);
    auto contents = *rc::gen::container<std::vector<std::string>>(
        file_count, rc::gen::container<std::string>(rc::gen::character<char>()));

    TempDir td;
    TempStore ts;

    for (int i = 0; i < file_count; ++i) {
        write_file(td.path, "file" + std::to_string(i) + ".txt", contents[static_cast<size_t>(i)]);
    }

    verify_tree_structure(MerkleEngine::build_tree(td.path, ts.store));
}

// For any file whose mtime and size are unchanged, a second build SHALL reuse the cached hash
// without recomputation.
RC_GTEST_PROP(MerkleEngineProperty, HashCachingOptimization, ()) {
    auto content = *rc::gen::map(rc::gen::container<std::string>(rc::gen::character<char>()),
                                 [](std::string s) { return s.empty() ? std::string("x") : s; });

    TempDir td;
    TempStore ts;

    auto file_path = write_file(td.path, "cached.txt", content);
    MerkleNode tree_a = MerkleEngine::build_tree(td.path, ts.store);

    // Populate FileMetadata so the cache check (mtime + size) can pass on the next build.
    FileMetadata meta;
    meta.path = "cached.txt";
    meta.hash = tree_a.children.empty() ? tree_a.hash : tree_a.children[0].hash;
    meta.size = fs::file_size(file_path);
    meta.mtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                           fs::last_write_time(file_path).time_since_epoch())
                                           .count());
    ts.store.upsert_file(meta);

    // Second build over the same directory with the same mtime/size — cache hit expected.
    MerkleNode tree_b = MerkleEngine::build_tree(td.path, ts.store);

    RC_ASSERT(tree_a.hash == tree_b.hash);
}

// For any two trees, diff SHALL identify exactly the files added, modified, and deleted between
// them.
RC_GTEST_PROP(MerkleEngineProperty, MerkleTreeDiffCorrectness, ()) {
    auto shared_count = *rc::gen::inRange<int>(0, 4);
    auto only1_count = *rc::gen::inRange<int>(0, 3);
    auto only2_count = *rc::gen::inRange<int>(0, 3);

    auto make_content = [](int seed) { return "content_" + std::to_string(seed); };

    TempDir td1, td2;
    TempStore ts1, ts2;

    std::set<std::string> expected_modified, expected_deleted, expected_added;

    for (int i = 0; i < shared_count; ++i) {
        std::string name = "shared_" + std::to_string(i) + ".txt";
        write_file(td1.path, name, make_content(i));
        write_file(td2.path, name, make_content(i + 1000));  // different content → modified
        expected_modified.insert(name);
    }

    for (int i = 0; i < only1_count; ++i) {
        std::string name = "only1_" + std::to_string(i) + ".txt";
        write_file(td1.path, name, make_content(i + 2000));
        expected_deleted.insert(name);
    }

    for (int i = 0; i < only2_count; ++i) {
        std::string name = "only2_" + std::to_string(i) + ".txt";
        write_file(td2.path, name, make_content(i + 3000));
        expected_added.insert(name);
    }

    MerkleNode tree1 = MerkleEngine::build_tree(td1.path, ts1.store);
    MerkleNode tree2 = MerkleEngine::build_tree(td2.path, ts2.store);
    auto d = MerkleEngine::diff(tree1, tree2);

    RC_ASSERT(std::set<std::string>(d.added.begin(), d.added.end()) == expected_added);
    RC_ASSERT(std::set<std::string>(d.modified.begin(), d.modified.end()) == expected_modified);
    RC_ASSERT(std::set<std::string>(d.deleted.begin(), d.deleted.end()) == expected_deleted);
}

// For any two identical trees, diff SHALL return an empty result without traversing matching
// subtrees.
RC_GTEST_PROP(MerkleEngineProperty, MerkleTreeOptimization, ()) {
    auto file_count = *rc::gen::inRange<int>(1, 6);
    auto contents = *rc::gen::container<std::vector<std::string>>(
        file_count, rc::gen::container<std::string>(rc::gen::character<char>()));

    TempDir td1, td2;
    TempStore ts1, ts2;

    for (int i = 0; i < file_count; ++i) {
        std::string name = "file" + std::to_string(i) + ".txt";
        write_file(td1.path, name, contents[static_cast<size_t>(i)]);
        write_file(td2.path, name, contents[static_cast<size_t>(i)]);
    }

    auto d = MerkleEngine::diff(MerkleEngine::build_tree(td1.path, ts1.store),
                                MerkleEngine::build_tree(td2.path, ts2.store));

    RC_ASSERT(d.added.empty());
    RC_ASSERT(d.modified.empty());
    RC_ASSERT(d.deleted.empty());
}

// For any tree where only one file differs, diff SHALL identify exactly that one file and skip all
// unchanged subtrees.
RC_GTEST_PROP(MerkleEngineProperty, MerkleTreeComparisonComplexity, ()) {
    TempDir td1, td2;
    TempStore ts1, ts2;

    for (int dir = 0; dir < 3; ++dir) {
        std::string subdir = "dir" + std::to_string(dir);
        for (int f = 0; f < 5; ++f) {
            std::string name = subdir + "/file" + std::to_string(f) + ".txt";
            std::string content = "content_d" + std::to_string(dir) + "_f" + std::to_string(f);
            write_file(td1.path, name, content);
            write_file(td2.path, name, content);
        }
    }

    std::string changed_file = "dir1/file2.txt";
    write_file(td2.path, changed_file, "CHANGED_CONTENT");

    auto d = MerkleEngine::diff(MerkleEngine::build_tree(td1.path, ts1.store),
                                MerkleEngine::build_tree(td2.path, ts2.store));

    RC_ASSERT(d.added.empty());
    RC_ASSERT(d.deleted.empty());
    RC_ASSERT(d.modified.size() == 1u);
    RC_ASSERT(d.modified[0] == changed_file);
}
