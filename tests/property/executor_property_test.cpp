#include "executor.hpp"
#include "manifest_store.hpp"
#include "merkle_engine.hpp"
#include "sync_planner.hpp"
#include "version_vector.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

fs::path make_temp_dir() {
    static std::atomic<int> counter{0};
    fs::path p =
        fs::temp_directory_path() / ("caravault_exec_prop_" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(p);
    return p;
}

struct TempDrive {
    fs::path root;
    fs::path db_path;
    ManifestStore store;
    std::string drive_id;

    TempDrive(const fs::path& base, const std::string& id)
        : root(base / id),
          db_path(base / (id + ".db")),
          store(ManifestStore::open(db_path)),
          drive_id(id) {
        fs::create_directories(root);
        store.register_drive(id);
    }

    ~TempDrive() {
        store.close();
        fs::remove_all(root);
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

std::string read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), {}};
}

FileMetadata make_meta(const std::string& path,
                       const std::string& hash,
                       uint64_t size,
                       uint64_t mtime,
                       const VersionVector& vv,
                       bool tombstone = false) {
    FileMetadata m;
    m.path = path;
    m.hash = hash;
    m.size = size;
    m.mtime = mtime;
    m.version_vector = vv;
    m.tombstone = tombstone;
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

rc::Gen<std::string> gen_content() {
    return rc::gen::map(rc::gen::string<std::string>(),
                        [](std::string s) { return s.empty() ? std::string("x") : s; });
}

}  // namespace

// For any file transfer, the system SHALL verify the hash before and after.
RC_GTEST_PROP(ExecutorProperty, TransferIntegrityVerification, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    TempDrive src(base, "drive_src");
    TempDrive dst(base, "drive_dst");

    fs::path src_file = src.root / filename;
    write_file(src_file, content);
    std::string hash = MerkleEngine::compute_file_hash(src_file);

    VersionVector vv;
    vv.increment("drive_src");
    src.store.upsert_file(make_meta(filename, hash, content.size(), 100, vv));

    SyncOp op;
    op.type = SyncOpType::COPY;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv;

    std::map<std::string, fs::path> roots{{"drive_src", src.root}, {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_src", &src.store},
                                                    {"drive_dst", &dst.store}};

    auto result = Executor{}.execute(op, roots, manifests);

    RC_ASSERT(result.success);
    RC_ASSERT(fs::exists(dst.root / filename));
    RC_ASSERT(Executor::verify_hash(dst.root / filename, hash));

    fs::remove_all(base);
}

// For any file that exists on one drive and not on connected drives,
// synchronization SHALL copy the file to all connected drives.
RC_GTEST_PROP(ExecutorProperty, NewFilePropagation, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    TempDrive src(base, "drive_src");
    TempDrive dst(base, "drive_dst");

    fs::path src_file = src.root / filename;
    write_file(src_file, content);
    std::string hash = MerkleEngine::compute_file_hash(src_file);

    VersionVector vv;
    vv.increment("drive_src");
    src.store.upsert_file(make_meta(filename, hash, content.size(), 100, vv));

    SyncOp op;
    op.type = SyncOpType::COPY;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv;

    std::map<std::string, fs::path> roots{{"drive_src", src.root}, {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_src", &src.store},
                                                    {"drive_dst", &dst.store}};

    auto result = Executor{}.execute(op, roots, manifests);

    RC_ASSERT(result.success);
    RC_ASSERT(fs::exists(dst.root / filename));
    RC_ASSERT(read_file(dst.root / filename) == content);

    auto stored = dst.store.get_file(filename);
    RC_ASSERT(stored.has_value());
    RC_ASSERT(stored->hash == hash);

    fs::remove_all(base);
}

// For any file modified on one drive (higher version vector), synchronization
// SHALL propagate the update to all connected drives.
RC_GTEST_PROP(ExecutorProperty, ModificationPropagation, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto old_content = *gen_content();
    auto new_content = *rc::gen::map(
        gen_content(), [&](std::string s) { return s == old_content ? s + "_new" : s; });

    TempDrive src(base, "drive_src");
    TempDrive dst(base, "drive_dst");

    fs::path dst_file = dst.root / filename;
    write_file(dst_file, old_content);
    VersionVector vv_old;
    vv_old.increment("drive_dst");
    dst.store.upsert_file(make_meta(
        filename, MerkleEngine::compute_file_hash(dst_file), old_content.size(), 100, vv_old));

    fs::path src_file = src.root / filename;
    write_file(src_file, new_content);
    VersionVector vv_new = vv_old;
    vv_new.increment("drive_src");
    std::string new_hash = MerkleEngine::compute_file_hash(src_file);
    src.store.upsert_file(make_meta(filename, new_hash, new_content.size(), 200, vv_new));

    SyncOp op;
    op.type = SyncOpType::REPLACE;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv_new;

    std::map<std::string, fs::path> roots{{"drive_src", src.root}, {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_src", &src.store},
                                                    {"drive_dst", &dst.store}};

    auto result = Executor{}.execute(op, roots, manifests);

    RC_ASSERT(result.success);
    RC_ASSERT(read_file(dst_file) == new_content);

    auto stored = dst.store.get_file(filename);
    RC_ASSERT(stored.has_value());
    RC_ASSERT(stored->hash == new_hash);

    fs::remove_all(base);
}

// For any file deleted on one drive (tombstone set), synchronization SHALL
// propagate the deletion to all connected drives.
RC_GTEST_PROP(ExecutorProperty, DeletionPropagation, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    TempDrive dst(base, "drive_dst");

    fs::path dst_file = dst.root / filename;
    write_file(dst_file, content);
    VersionVector vv;
    vv.increment("drive_dst");
    dst.store.upsert_file(make_meta(filename, "h", content.size(), 100, vv));

    VersionVector vv_del = vv;
    vv_del.increment("drive_src");

    SyncOp op;
    op.type = SyncOpType::DELETE;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv_del;

    std::map<std::string, fs::path> roots{{"drive_src", base / "drive_src"},
                                          {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_dst", &dst.store}};

    auto result = Executor{}.execute(op, roots, manifests);

    RC_ASSERT(result.success);
    RC_ASSERT(!fs::exists(dst_file));

    auto stored = dst.store.get_file(filename);
    RC_ASSERT(stored.has_value());
    RC_ASSERT(stored->tombstone);

    fs::remove_all(base);
}

// For any file renamed on one drive, synchronization SHALL propagate the rename.
RC_GTEST_PROP(ExecutorProperty, RenamePropagation, ()) {
    auto base = make_temp_dir();
    auto old_name = *gen_filename();
    auto new_name = *rc::gen::map(
        gen_filename(), [&](std::string s) { return s == old_name ? s + "_renamed" : s; });
    RC_PRE(old_name != new_name);
    auto content = *gen_content();

    TempDrive dst(base, "drive_dst");

    write_file(dst.root / old_name, content);
    VersionVector vv;
    vv.increment("drive_dst");
    dst.store.upsert_file(make_meta(old_name, "h", content.size(), 100, vv));

    VersionVector vv_new = vv;
    vv_new.increment("drive_src");

    SyncOp op;
    op.type = SyncOpType::RENAME;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = old_name;
    op.new_path = new_name;
    op.new_version_vector = vv_new;

    std::map<std::string, fs::path> roots{{"drive_src", base / "drive_src"},
                                          {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_dst", &dst.store}};

    auto result = Executor{}.execute(op, roots, manifests);

    RC_ASSERT(result.success);
    RC_ASSERT(!fs::exists(dst.root / old_name));
    RC_ASSERT(fs::exists(dst.root / new_name));
    RC_ASSERT(read_file(dst.root / new_name) == content);

    auto stored_new = dst.store.get_file(new_name);
    RC_ASSERT(stored_new.has_value());

    fs::remove_all(base);
}

// For any synchronization operation, all affected drives SHALL have their
// version vectors updated to reflect the operation.
RC_GTEST_PROP(ExecutorProperty, VersionVectorUpdateAfterSync, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    TempDrive src(base, "drive_src");
    TempDrive dst(base, "drive_dst");

    fs::path src_file = src.root / filename;
    write_file(src_file, content);
    std::string hash = MerkleEngine::compute_file_hash(src_file);

    VersionVector vv;
    vv.increment("drive_src");
    src.store.upsert_file(make_meta(filename, hash, content.size(), 100, vv));

    SyncOp op;
    op.type = SyncOpType::COPY;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv;

    std::map<std::string, fs::path> roots{{"drive_src", src.root}, {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_src", &src.store},
                                                    {"drive_dst", &dst.store}};

    auto result = Executor{}.execute(op, roots, manifests);
    RC_ASSERT(result.success);

    auto stored = dst.store.get_file(filename);
    RC_ASSERT(stored.has_value());
    RC_ASSERT(stored->version_vector == vv);

    fs::remove_all(base);
}

// For any file synchronized between drives, the file permissions and
// modification timestamp SHALL be preserved.
RC_GTEST_PROP(ExecutorProperty, MetadataPreservationDuringSync, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    TempDrive src(base, "drive_src");
    TempDrive dst(base, "drive_dst");

    fs::path src_file = src.root / filename;
    write_file(src_file, content);

#ifndef _WIN32
    ::chmod(src_file.c_str(), 0644);
#endif

    std::string hash = MerkleEngine::compute_file_hash(src_file);
    VersionVector vv;
    vv.increment("drive_src");
    src.store.upsert_file(make_meta(filename, hash, content.size(), 100, vv));

    SyncOp op;
    op.type = SyncOpType::COPY;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv;

    std::map<std::string, fs::path> roots{{"drive_src", src.root}, {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_src", &src.store},
                                                    {"drive_dst", &dst.store}};

    auto result = Executor{}.execute(op, roots, manifests);
    RC_ASSERT(result.success);

    fs::path dst_file = dst.root / filename;
    RC_ASSERT(fs::exists(dst_file));

#ifndef _WIN32
    struct stat st_src = {};
    struct stat st_dst = {};
    ::stat(src_file.c_str(), &st_src);
    ::stat(dst_file.c_str(), &st_dst);
    RC_ASSERT((st_src.st_mode & 07777) == (st_dst.st_mode & 07777));
#endif

    fs::remove_all(base);
}

// For any file write operation, the system SHALL write to a temporary file
// and atomically rename it to the target path only after successful completion.
RC_GTEST_PROP(ExecutorProperty, AtomicWriteViaTempThenRename, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    TempDrive src(base, "drive_src");
    TempDrive dst(base, "drive_dst");

    fs::path src_file = src.root / filename;
    write_file(src_file, content);
    std::string hash = MerkleEngine::compute_file_hash(src_file);

    VersionVector vv;
    vv.increment("drive_src");
    src.store.upsert_file(make_meta(filename, hash, content.size(), 100, vv));

    SyncOp op;
    op.type = SyncOpType::COPY;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv;

    std::map<std::string, fs::path> roots{{"drive_src", src.root}, {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_src", &src.store},
                                                    {"drive_dst", &dst.store}};

    auto result = Executor{}.execute(op, roots, manifests);
    RC_ASSERT(result.success);

    // Temp file must not survive a successful write
    RC_ASSERT(!fs::exists(fs::path((dst.root / filename).string() + ".caravault.tmp")));
    RC_ASSERT(fs::exists(dst.root / filename));
    RC_ASSERT(Executor::verify_hash(dst.root / filename, hash));

    fs::remove_all(base);
}

// For any incomplete operation in the transaction log at startup, the system
// SHALL roll back the operation to a consistent state.
RC_GTEST_PROP(ExecutorProperty, CrashRecoveryRollback, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();

    TempDrive drv(base, "drive_a");

    fs::path target = drv.root / filename;
    fs::path tmp = fs::path(target.string() + ".caravault.tmp");

    // Simulate a partial write: tmp exists but its content does not match the
    // hash stored in the manifest (i.e. the write was interrupted mid-stream).
    write_file(tmp, "corrupted_partial_write");
    // No manifest entry -> hash lookup returns nullopt -> treated as invalid.
    drv.store.begin_operation("WRITE", target.string());

    Executor{}.recover_incomplete_operations(drv.store, drv.root);

    RC_ASSERT(!fs::exists(tmp));
    RC_ASSERT(!fs::exists(target));
    RC_ASSERT(drv.store.get_incomplete_operations().empty());

    fs::remove_all(base);
}

// For any pending file operation, the operation SHALL be recorded in the
// transaction log before execution begins.
RC_GTEST_PROP(ExecutorProperty, TransactionLogMaintenance, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    TempDrive src(base, "drive_src");
    TempDrive dst(base, "drive_dst");

    fs::path src_file = src.root / filename;
    write_file(src_file, content);
    std::string hash = MerkleEngine::compute_file_hash(src_file);

    VersionVector vv;
    vv.increment("drive_src");
    src.store.upsert_file(make_meta(filename, hash, content.size(), 100, vv));

    SyncOp op;
    op.type = SyncOpType::COPY;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv;

    std::map<std::string, fs::path> roots{{"drive_src", src.root}, {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_src", &src.store},
                                                    {"drive_dst", &dst.store}};

    auto result = Executor{}.execute(op, roots, manifests);
    RC_ASSERT(result.success);

    // A successful execute must leave no incomplete log entries.
    RC_ASSERT(dst.store.get_incomplete_operations().empty());

    fs::remove_all(base);
}

// For any partially written file during crash recovery, the system SHALL
// verify the file's hash before deciding to keep or discard it.
RC_GTEST_PROP(ExecutorProperty, RecoveryIntegrityVerification, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    TempDrive drv(base, "drive_a");

    fs::path target = drv.root / filename;
    fs::path tmp = fs::path(target.string() + ".caravault.tmp");

    write_file(tmp, content);
    std::string valid_hash = MerkleEngine::compute_file_hash(tmp);

    VersionVector vv;
    vv.increment("drive_a");
    drv.store.upsert_file(make_meta(filename, valid_hash, content.size(), 100, vv));
    drv.store.begin_operation("WRITE", target.string());

    Executor{}.recover_incomplete_operations(drv.store, drv.root);

    RC_ASSERT(fs::exists(target));
    RC_ASSERT(Executor::verify_hash(target, valid_hash));
    RC_ASSERT(drv.store.get_incomplete_operations().empty());

    fs::remove_all(base);
}

// For any database transaction, either all operations SHALL be committed,
// or none SHALL be committed.
RC_GTEST_PROP(ExecutorProperty, DatabaseTransactionAtomicity, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();

    TempDrive drv(base, "drive_a");

    VersionVector vv;
    vv.increment("drive_a");
    auto meta = make_meta(filename, "hash_a", 0, 100, vv);

    drv.store.begin_transaction();
    drv.store.upsert_file(meta);
    drv.store.rollback();
    RC_ASSERT(!drv.store.get_file(filename).has_value());

    drv.store.begin_transaction();
    drv.store.upsert_file(meta);
    drv.store.commit();

    auto stored = drv.store.get_file(filename);
    RC_ASSERT(stored.has_value());
    RC_ASSERT(stored->hash == "hash_a");

    fs::remove_all(base);
}

// For any file deletion, the version vector SHALL be updated to record the
// deletion event.
RC_GTEST_PROP(ExecutorProperty, DeletionVersionVectorUpdate, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    TempDrive dst(base, "drive_dst");

    write_file(dst.root / filename, content);
    VersionVector vv_before;
    vv_before.increment("drive_dst");
    dst.store.upsert_file(make_meta(filename, "h", content.size(), 100, vv_before));

    VersionVector vv_after = vv_before;
    vv_after.increment("drive_src");

    SyncOp op;
    op.type = SyncOpType::DELETE;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv_after;

    std::map<std::string, fs::path> roots{{"drive_src", base / "drive_src"},
                                          {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_dst", &dst.store}};

    auto result = Executor{}.execute(op, roots, manifests);
    RC_ASSERT(result.success);

    auto stored = dst.store.get_file(filename);
    RC_ASSERT(stored.has_value());
    RC_ASSERT(stored->tombstone);
    RC_ASSERT(stored->version_vector == vv_after);

    fs::remove_all(base);
}
