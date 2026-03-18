#include "manifest_store.hpp"
#include "sync_planner.hpp"
#include "version_vector.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

fs::path make_temp_db_path() {
    static std::atomic<int> counter{0};
    return fs::temp_directory_path() /
           ("caravault_sp_prop_" + std::to_string(counter.fetch_add(1)) + ".db");
}

struct TempStore {
    fs::path path;
    ManifestStore store;

    TempStore() : path(make_temp_db_path()), store(ManifestStore::open(path)) {}

    ~TempStore() {
        store.close();
        fs::remove(path);
        fs::remove(fs::path(path.string() + "-wal"));
        fs::remove(fs::path(path.string() + "-shm"));
    }
};

FileMetadata make_meta(const std::string& file_path,
                       const std::string& hash,
                       uint64_t mtime,
                       const VersionVector& vv,
                       bool tombstone = false,
                       std::optional<uint64_t> inode = std::nullopt) {
    FileMetadata m;
    m.path = file_path;
    m.hash = hash;
    m.size = 100;
    m.mtime = mtime;
    m.version_vector = vv;
    m.tombstone = tombstone;
    m.inode = inode;
    return m;
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

bool has_op_type(const std::vector<SyncOp>& ops,
                 SyncOpType type,
                 const std::string& path,
                 const std::string& target) {
    return std::any_of(ops.begin(), ops.end(), [&](const SyncOp& op) {
        return op.type == type && op.path == path && op.target_drive_id == target;
    });
}

}  // namespace

// For any file operation (addition, modification, deletion, rename), the change
// detection SHALL correctly identify the operation type.
RC_GTEST_PROP(SyncPlannerProperty, ChangeTypeDetection, ()) {
    auto filename = *gen_filename();

    TempStore ts_a, ts_b;
    ts_a.store.register_drive("drive_a");
    ts_b.store.register_drive("drive_b");

    // drive_a has the file; drive_b does not -> expect COPY to drive_b.
    VersionVector vv_a;
    vv_a.increment("drive_a");
    ts_a.store.upsert_file(make_meta(filename, "hash1", 100, vv_a));

    std::map<std::string, ManifestStore*> manifests{{"drive_a", &ts_a.store},
                                                    {"drive_b", &ts_b.store}};
    auto ops = SyncPlanner{}.plan_sync(manifests, {});

    RC_ASSERT(has_op_type(ops, SyncOpType::COPY, filename, "drive_b"));

    // Now drive_b has an older version -> expect REPLACE.
    VersionVector vv_b;
    vv_b.increment("drive_b");
    ts_b.store.upsert_file(make_meta(filename, "hash_old", 50, vv_b));

    VersionVector vv_a2 = vv_b;
    vv_a2.increment("drive_a");
    ts_a.store.upsert_file(make_meta(filename, "hash2", 200, vv_a2));

    auto ops2 = SyncPlanner{}.plan_sync(manifests, {});
    RC_ASSERT(has_op_type(ops2, SyncOpType::REPLACE, filename, "drive_b"));

    // drive_a tombstones the file -> expect DELETE on drive_b.
    VersionVector vv_del = vv_a2;
    vv_del.increment("drive_a");
    ts_a.store.upsert_file(make_meta(filename, "", 300, vv_del, /*tombstone=*/true));

    auto ops3 = SyncPlanner{}.plan_sync(manifests, {});
    RC_ASSERT(has_op_type(ops3, SyncOpType::REMOVE, filename, "drive_b"));
}

// For any file that is renamed (same content, different path), the system SHALL
// detect it as a RENAME operation rather than DELETE followed by ADD.
RC_GTEST_PROP(SyncPlannerProperty, RenameDetectionByContentHash, ()) {
    auto old_name = *gen_filename();
    auto new_name = *rc::gen::map(gen_filename(),
                                  [&](std::string s) { return s == old_name ? s + "_new" : s; });
    RC_PRE(old_name != new_name);

    const std::string hash = "content_hash_abc";

    TempStore ts_a, ts_b;
    ts_a.store.register_drive("drive_a");
    ts_b.store.register_drive("drive_b");

    // drive_a: file at new_name (renamed); tombstone the old name on drive_a
    VersionVector vv_a;
    vv_a.increment("drive_a");
    ts_a.store.upsert_file(make_meta(new_name, hash, 200, vv_a));
    // Record the tombstone so detect_renames can confirm the rename intent.
    VersionVector vv_tomb = vv_a;
    vv_tomb.increment("drive_a");
    ts_a.store.upsert_file(make_meta(old_name, "", 0, vv_tomb, /*tombstone=*/true));

    // drive_b: file at old_name (original location, same hash)
    VersionVector vv_b;
    vv_b.increment("drive_b");
    ts_b.store.upsert_file(make_meta(old_name, hash, 100, vv_b));

    std::map<std::string, ManifestStore*> manifests{{"drive_a", &ts_a.store},
                                                    {"drive_b", &ts_b.store}};
    auto ops = SyncPlanner{}.plan_sync(manifests, {});

    // There should be a RENAME on drive_b (old_name -> new_name),
    // not a DELETE + COPY pair.
    bool has_rename = std::any_of(ops.begin(), ops.end(), [&](const SyncOp& op) {
        return op.type == SyncOpType::RENAME && op.path == old_name && op.new_path == new_name &&
               op.target_drive_id == "drive_b";
    });
    bool has_delete_old = has_op_type(ops, SyncOpType::REMOVE, old_name, "drive_b");
    bool has_copy_new = has_op_type(ops, SyncOpType::COPY, new_name, "drive_b");

    RC_ASSERT(has_rename);
    RC_ASSERT(!has_delete_old);
    RC_ASSERT(!has_copy_new);
}

// For any file that is moved to a different directory (same content, different path),
// the system SHALL detect it as a RENAME/move operation.
RC_GTEST_PROP(SyncPlannerProperty, MoveDetectionByContentHash, ()) {
    auto filename = *gen_filename();
    const std::string src_path = "dir_a/" + filename;
    const std::string dst_path = "dir_b/" + filename;
    const std::string hash = "move_hash_xyz";

    TempStore ts_a, ts_b;
    ts_a.store.register_drive("drive_a");
    ts_b.store.register_drive("drive_b");

    // drive_a: file at dst_path (moved); tombstone the src_path on drive_a
    VersionVector vv_a;
    vv_a.increment("drive_a");
    ts_a.store.upsert_file(make_meta(dst_path, hash, 200, vv_a));
    VersionVector vv_tomb = vv_a;
    vv_tomb.increment("drive_a");
    ts_a.store.upsert_file(make_meta(src_path, "", 0, vv_tomb, /*tombstone=*/true));

    // drive_b: file at src_path (original location)
    VersionVector vv_b;
    vv_b.increment("drive_b");
    ts_b.store.upsert_file(make_meta(src_path, hash, 100, vv_b));

    std::map<std::string, ManifestStore*> manifests{{"drive_a", &ts_a.store},
                                                    {"drive_b", &ts_b.store}};
    auto ops = SyncPlanner{}.plan_sync(manifests, {});

    // Expect a RENAME on drive_b from src_path to dst_path.
    bool has_rename = std::any_of(ops.begin(), ops.end(), [&](const SyncOp& op) {
        return op.type == SyncOpType::RENAME && op.path == src_path && op.new_path == dst_path &&
               op.target_drive_id == "drive_b";
    });
    RC_ASSERT(has_rename);
}

// When inode information is available and a file has the same inode but a
// different path, the system SHALL detect it as a rename.
RC_GTEST_PROP(SyncPlannerProperty, InodeBasedRenameDetection, ()) {
    auto old_name = *gen_filename();
    auto new_name = *rc::gen::map(
        gen_filename(), [&](std::string s) { return s == old_name ? s + "_renamed" : s; });
    RC_PRE(old_name != new_name);

    const std::string hash = "inode_hash_123";
    const uint64_t inode_val = 42;

    TempStore ts_a, ts_b;
    ts_a.store.register_drive("drive_a");
    ts_b.store.register_drive("drive_b");

    // drive_a: file at new_name with inode; tombstone the old name on drive_a
    VersionVector vv_a;
    vv_a.increment("drive_a");
    ts_a.store.upsert_file(make_meta(new_name, hash, 200, vv_a, false, inode_val));
    VersionVector vv_tomb = vv_a;
    vv_tomb.increment("drive_a");
    ts_a.store.upsert_file(make_meta(old_name, "", 0, vv_tomb, /*tombstone=*/true));

    // drive_b: file at old_name with same hash and inode
    VersionVector vv_b;
    vv_b.increment("drive_b");
    ts_b.store.upsert_file(make_meta(old_name, hash, 100, vv_b, false, inode_val));

    std::map<std::string, ManifestStore*> manifests{{"drive_a", &ts_a.store},
                                                    {"drive_b", &ts_b.store}};
    auto ops = SyncPlanner{}.plan_sync(manifests, {});

    // The planner should detect a rename (same hash -> rename detected).
    bool has_rename = std::any_of(ops.begin(), ops.end(), [&](const SyncOp& op) {
        return op.type == SyncOpType::RENAME && op.target_drive_id == "drive_b";
    });
    RC_ASSERT(has_rename);
}

// Additional: MKDIR ops precede file ops in the same directory.
RC_GTEST_PROP(SyncPlannerProperty, MkdirPrecedesFileOps, ()) {
    auto dir = *gen_filename();
    auto file = *gen_filename();
    const std::string path = dir + "/" + file;

    TempStore ts_a, ts_b;
    ts_a.store.register_drive("drive_a");
    ts_b.store.register_drive("drive_b");

    VersionVector vv;
    vv.increment("drive_a");
    ts_a.store.upsert_file(make_meta(path, "h", 100, vv));

    std::map<std::string, ManifestStore*> manifests{{"drive_a", &ts_a.store},
                                                    {"drive_b", &ts_b.store}};
    auto ops = SyncPlanner{}.plan_sync(manifests, {});

    // Find positions of MKDIR(dir) and COPY(path) on drive_b.
    int mkdir_pos = -1, copy_pos = -1;
    for (int i = 0; i < static_cast<int>(ops.size()); ++i) {
        if (ops[i].target_drive_id == "drive_b") {
            if (ops[i].type == SyncOpType::MKDIR && ops[i].path == dir)
                mkdir_pos = i;
            if (ops[i].type == SyncOpType::COPY && ops[i].path == path)
                copy_pos = i;
        }
    }

    RC_ASSERT(mkdir_pos != -1);
    RC_ASSERT(copy_pos != -1);
    RC_ASSERT(mkdir_pos < copy_pos);
}

// Additional: No ops generated when drives are already in sync.
RC_GTEST_PROP(SyncPlannerProperty, NoOpsWhenAlreadyInSync, ()) {
    auto filename = *gen_filename();

    TempStore ts_a, ts_b;
    ts_a.store.register_drive("drive_a");
    ts_b.store.register_drive("drive_b");

    VersionVector vv;
    vv.increment("drive_a");
    auto meta = make_meta(filename, "same_hash", 100, vv);
    ts_a.store.upsert_file(meta);
    ts_b.store.upsert_file(meta);

    std::map<std::string, ManifestStore*> manifests{{"drive_a", &ts_a.store},
                                                    {"drive_b", &ts_b.store}};
    auto ops = SyncPlanner{}.plan_sync(manifests, {});

    // No COPY or REPLACE should be generated for this file.
    bool has_copy_or_replace = std::any_of(ops.begin(), ops.end(), [&](const SyncOp& op) {
        return (op.type == SyncOpType::COPY || op.type == SyncOpType::REPLACE) &&
               op.path == filename;
    });
    RC_ASSERT(!has_copy_or_replace);
}
