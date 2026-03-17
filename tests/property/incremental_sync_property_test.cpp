#include "manifest_store.hpp"
#include "merkle_engine.hpp"
#include "sync_planner.hpp"
#include "version_vector.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

std::atomic<int> g_counter{0};

fs::path make_temp_dir() {
    fs::path p =
        fs::temp_directory_path() / ("caravault_inc_" + std::to_string(g_counter.fetch_add(1)));
    fs::create_directories(p);
    return p;
}

fs::path make_temp_db() {
    return fs::temp_directory_path() /
           ("caravault_inc_db_" + std::to_string(g_counter.fetch_add(1)) + ".db");
}

struct TempDir {
    fs::path path;
    TempDir() : path(make_temp_dir()) {}
    ~TempDir() { fs::remove_all(path); }
};

struct TempStore {
    fs::path db_path;
    ManifestStore store;
    TempStore() : db_path(make_temp_db()), store(ManifestStore::open(db_path)) {}
    ~TempStore() {
        store.close();
        fs::remove(db_path);
        fs::remove(fs::path(db_path.string() + "-wal"));
        fs::remove(fs::path(db_path.string() + "-shm"));
    }
};

void write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

rc::Gen<std::string> gen_nonempty_content() {
    return rc::gen::map(rc::gen::container<std::string>(rc::gen::character<char>()),
                        [](std::string s) { return s.empty() ? std::string("x") : s; });
}

rc::Gen<std::string> gen_filename() {
    return rc::gen::map(rc::gen::string<std::string>(), [](std::string s) {
        std::string out;
        for (char c : s)
            if (std::isalnum(static_cast<unsigned char>(c)))
                out += c;
        return out.empty() ? std::string("file") : out;
    });
}

FileMetadata make_meta(const std::string& path,
                       const std::string& hash,
                       uint64_t size,
                       uint64_t mtime,
                       const VersionVector& vv) {
    FileMetadata m;
    m.path = path;
    m.hash = hash;
    m.size = size;
    m.mtime = mtime;
    m.version_vector = vv;
    return m;
}

}  // namespace

// For any drive, after scanning, the set of files in the manifest SHALL equal
// the set of actual files on the drive (excluding configured exclusions).
RC_GTEST_PROP(IncrementalSyncProperty, FileScanCompleteness, ()) {
    auto file_count = *rc::gen::inRange<int>(1, 8);
    auto names = *rc::gen::container<std::vector<std::string>>(file_count, gen_filename());
    auto contents =
        *rc::gen::container<std::vector<std::string>>(file_count, gen_nonempty_content());

    TempDir td;
    TempStore ts;

    std::set<std::string> expected_paths;
    for (int i = 0; i < file_count; ++i) {
        std::string name = names[static_cast<size_t>(i)] + std::to_string(i) + ".txt";
        write_file(td.path / name, contents[static_cast<size_t>(i)]);
        expected_paths.insert(name);
    }

    MerkleEngine::build_tree(td.path, ts.store);

    std::set<std::string> manifest_paths;
    for (const auto& f : ts.store.get_all_files())
        if (!f.tombstone)
            manifest_paths.insert(f.path);

    RC_ASSERT(manifest_paths == expected_paths);
}

// For any scanned file, the stored metadata SHALL contain path, hash, size,
// and modification time fields.
RC_GTEST_PROP(IncrementalSyncProperty, MetadataCompleteness, ()) {
    auto content = *gen_nonempty_content();
    auto name = *gen_filename();

    TempDir td;
    TempStore ts;

    write_file(td.path / (name + ".txt"), content);
    MerkleEngine::build_tree(td.path, ts.store);

    auto meta = ts.store.get_file(name + ".txt");
    RC_ASSERT(meta.has_value());
    RC_ASSERT(!meta->path.empty());
    RC_ASSERT(!meta->hash.empty());
    RC_ASSERT(meta->size == content.size());
    RC_ASSERT(meta->mtime > 0u);
}

// For any file, after scanning, the computed hash SHALL be persisted in the
// manifest database and retrievable on subsequent queries.
RC_GTEST_PROP(IncrementalSyncProperty, HashCachePersistence, ()) {
    auto content = *gen_nonempty_content();
    auto name = *gen_filename();

    TempDir td;
    TempStore ts;

    auto file_path = td.path / (name + ".txt");
    write_file(file_path, content);

    MerkleEngine::build_tree(td.path, ts.store);

    auto meta = ts.store.get_file(name + ".txt");
    RC_ASSERT(meta.has_value());
    RC_ASSERT(!meta->hash.empty());

    std::string expected_hash = MerkleEngine::compute_file_hash(file_path);
    RC_ASSERT(meta->hash == expected_hash);

    auto merkle_hash = ts.store.get_merkle_hash(name + ".txt", 0);
    RC_ASSERT(merkle_hash.has_value());
    RC_ASSERT(*merkle_hash == expected_hash);
}

// For any file with unchanged modification time and size, scanning twice SHALL
// reuse the cached hash value without recomputation.
RC_GTEST_PROP(IncrementalSyncProperty, UnchangedFileHashReuse, ()) {
    auto content = *gen_nonempty_content();
    auto name = *gen_filename();

    TempDir td;
    TempStore ts;

    write_file(td.path / (name + ".txt"), content);

    MerkleNode tree1 = MerkleEngine::build_tree(td.path, ts.store);
    MerkleNode tree2 = MerkleEngine::build_tree(td.path, ts.store);

    RC_ASSERT(tree1.hash == tree2.hash);

    auto meta1 = ts.store.get_file(name + ".txt");
    auto meta2 = ts.store.get_file(name + ".txt");
    RC_ASSERT(meta1.has_value() && meta2.has_value());
    RC_ASSERT(meta1->hash == meta2->hash);
}

// For any file whose modification time or size has changed, scanning SHALL
// recompute the hash rather than reusing the cached value.
RC_GTEST_PROP(IncrementalSyncProperty, ChangedFileHashRecomputation, ()) {
    auto content1 = *gen_nonempty_content();
    auto content2 = *rc::gen::map(
        gen_nonempty_content(), [&](std::string s) { return s == content1 ? s + "_changed" : s; });
    RC_PRE(content1 != content2);

    auto name = *gen_filename();

    TempDir td;
    TempStore ts;

    auto file_path = td.path / (name + ".txt");
    write_file(file_path, content1);

    MerkleEngine::build_tree(td.path, ts.store);
    auto meta1 = ts.store.get_file(name + ".txt");
    RC_ASSERT(meta1.has_value());

    write_file(file_path, content2);
    auto new_time = fs::last_write_time(file_path) + std::chrono::seconds(2);
    fs::last_write_time(file_path, new_time);

    MerkleEngine::build_tree(td.path, ts.store);
    auto meta2 = ts.store.get_file(name + ".txt");
    RC_ASSERT(meta2.has_value());

    RC_ASSERT(meta2->hash == MerkleEngine::compute_file_hash(file_path));
    RC_ASSERT(meta1->hash != meta2->hash);
}

// For any two drives where only a subset of files differ, the sync planner
// SHALL generate transfer operations only for the changed files.
RC_GTEST_PROP(IncrementalSyncProperty, IncrementalTransfer, ()) {
    auto shared_count = *rc::gen::inRange<int>(1, 5);
    auto changed_count = *rc::gen::inRange<int>(1, 3);

    TempStore ts_a, ts_b;
    ts_a.store.register_drive("drive_a");
    ts_b.store.register_drive("drive_b");

    std::set<std::string> shared_paths;
    for (int i = 0; i < shared_count; ++i) {
        std::string path = "shared_" + std::to_string(i) + ".txt";
        VersionVector vv;
        vv.increment("drive_a");
        auto m = make_meta(path, "shared_hash_" + std::to_string(i), 100, 1000, vv);
        ts_a.store.upsert_file(m);
        ts_b.store.upsert_file(m);
        shared_paths.insert(path);
    }

    std::set<std::string> changed_paths;
    for (int i = 0; i < changed_count; ++i) {
        std::string path = "changed_" + std::to_string(i) + ".txt";

        VersionVector vv_old;
        vv_old.increment("drive_b");
        ts_b.store.upsert_file(make_meta(path, "old_hash_" + std::to_string(i), 50, 500, vv_old));

        VersionVector vv_new = vv_old;
        vv_new.increment("drive_a");
        ts_a.store.upsert_file(make_meta(path, "new_hash_" + std::to_string(i), 60, 600, vv_new));

        changed_paths.insert(path);
    }

    std::map<std::string, ManifestStore*> manifests{{"drive_a", &ts_a.store},
                                                    {"drive_b", &ts_b.store}};
    auto ops = SyncPlanner{}.plan_sync(manifests, {});

    for (const auto& path : shared_paths) {
        bool has_transfer = std::any_of(ops.begin(), ops.end(), [&](const SyncOp& op) {
            return (op.type == SyncOpType::COPY || op.type == SyncOpType::REPLACE) &&
                   op.path == path;
        });
        RC_ASSERT(!has_transfer);
    }

    for (const auto& path : changed_paths) {
        bool has_replace = std::any_of(ops.begin(), ops.end(), [&](const SyncOp& op) {
            return op.type == SyncOpType::REPLACE && op.path == path &&
                   op.target_drive_id == "drive_b";
        });
        RC_ASSERT(has_replace);
    }
}
