#include "executor.hpp"
#include "manifest_store.hpp"
#include "merkle_engine.hpp"
#include "version_vector.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using namespace caravault;

namespace {

fs::path make_temp_dir() {
    static std::atomic<int> counter{0};
    fs::path p =
        fs::temp_directory_path() / ("caravault_err_prop_" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(p);
    return p;
}

void write_file(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

FileMetadata make_meta(const std::string& path,
                       const std::string& hash,
                       uint64_t size,
                       const VersionVector& vv) {
    FileMetadata m;
    m.path = path;
    m.hash = hash;
    m.size = size;
    m.mtime = 100;
    m.version_vector = vv;
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

// For any database write failure, the system SHALL abort the current operation
// without partial commits.
RC_GTEST_PROP(ErrorHandlingProperty, DatabaseWriteFailureHandling, ()) {
    auto base = make_temp_dir();
    auto db_path = base / "test.db";

    auto store = ManifestStore::open(db_path);

    VersionVector vv;
    vv.increment("drive_a");
    auto meta_a = make_meta("file_a.txt", "hash_a", 10, vv);
    auto meta_b = make_meta("file_b.txt", "hash_b", 20, vv);

    store.begin_transaction();
    store.upsert_file(meta_a);
    store.upsert_file(meta_b);
    store.rollback();

    // Neither write should have persisted after rollback.
    RC_ASSERT(!store.get_file("file_a.txt").has_value());
    RC_ASSERT(!store.get_file("file_b.txt").has_value());

    store.begin_transaction();
    store.upsert_file(meta_a);
    store.upsert_file(meta_b);
    store.commit();

    RC_ASSERT(store.get_file("file_a.txt").has_value());
    RC_ASSERT(store.get_file("file_b.txt").has_value());

    store.close();
    fs::remove_all(base);
}

// For any drive scan where some files cannot be read, the system SHALL skip
// those files and continue scanning remaining files.
RC_GTEST_PROP(ErrorHandlingProperty, ErrorResilienceDuringScan, ()) {
    auto base = make_temp_dir();
    auto root = base / "drive";
    fs::create_directories(root);

    auto filename_ok = *gen_filename();
    auto filename_bad = *rc::gen::map(
        gen_filename(), [&](std::string s) { return s == filename_ok ? s + "_bad" : s; });
    RC_PRE(filename_ok != filename_bad);

    auto content = *gen_content();
    write_file(root / filename_ok, content);
    write_file(root / filename_bad, content);

#ifndef _WIN32
    ::chmod((root / filename_bad).c_str(), 0000);
#endif

    auto store = ManifestStore::open(base / "scan.db");
    std::vector<ScanError> errors;
    auto tree = MerkleEngine::build_tree(root, store, errors);

#ifndef _WIN32
    bool found_ok = false;
    for (const auto& child : tree.children) {
        if (child.path == filename_ok && !child.hash.empty())
            found_ok = true;
    }
    RC_ASSERT(found_ok);
    RC_ASSERT(!errors.empty());

    ::chmod((root / filename_bad).c_str(), 0644);
#else
    (void)tree;
#endif

    store.close();
    fs::remove_all(base);
}

// When a drive becomes disconnected during synchronization, the system SHALL
// safely abort operations on that drive and return an error result.
RC_GTEST_PROP(ErrorHandlingProperty, DriveDisconnectionHandling, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    fs::path src_root = base / "drive_src";
    fs::create_directories(src_root);
    auto src_store = ManifestStore::open(base / "drive_src.db");
    src_store.register_drive("drive_src");

    write_file(src_root / filename, content);
    std::string hash = MerkleEngine::compute_file_hash(src_root / filename);
    VersionVector vv;
    vv.increment("drive_src");
    src_store.upsert_file(make_meta(filename, hash, content.size(), vv));

    // Target root does not exist — simulates a disconnected drive.
    fs::path disconnected_root = base / "drive_disconnected_NONEXISTENT";
    auto dst_store = ManifestStore::open(base / "drive_dst.db");

    SyncOp op;
    op.type = SyncOpType::COPY;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv;

    std::map<std::string, fs::path> roots{{"drive_src", src_root},
                                          {"drive_dst", disconnected_root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_src", &src_store},
                                                    {"drive_dst", &dst_store}};

    auto result = Executor{}.execute(op, roots, manifests);

    RC_ASSERT(!result.success);
    RC_ASSERT(!result.error_message.empty());

    src_store.close();
    dst_store.close();
    fs::remove_all(base);
}

// When a file write fails (e.g. permission denied on destination), the system
// SHALL return a descriptive error and leave no partial temp files behind.
RC_GTEST_PROP(ErrorHandlingProperty, WriteFailureErrorHandling, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto content = *gen_content();

    fs::path src_root = base / "drive_src";
    fs::path dst_root = base / "drive_dst";
    fs::create_directories(src_root);
    fs::create_directories(dst_root);

    auto src_store = ManifestStore::open(base / "src.db");
    auto dst_store = ManifestStore::open(base / "dst.db");

    write_file(src_root / filename, content);
    std::string hash = MerkleEngine::compute_file_hash(src_root / filename);
    VersionVector vv;
    vv.increment("drive_src");
    src_store.upsert_file(make_meta(filename, hash, content.size(), vv));

#ifndef _WIN32
    ::chmod(dst_root.c_str(), 0555);
#endif

    SyncOp op;
    op.type = SyncOpType::COPY;
    op.source_drive_id = "drive_src";
    op.target_drive_id = "drive_dst";
    op.path = filename;
    op.new_version_vector = vv;

    std::map<std::string, fs::path> roots{{"drive_src", src_root}, {"drive_dst", dst_root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_src", &src_store},
                                                    {"drive_dst", &dst_store}};

    auto result = Executor{}.execute(op, roots, manifests);

#ifndef _WIN32
    ::chmod(dst_root.c_str(), 0755);

    if (!result.success) {
        RC_ASSERT(!result.error_message.empty());
        fs::path tmp = fs::path((dst_root / filename).string() + ".caravault.tmp");
        RC_ASSERT(!fs::exists(tmp));
    }
#else
    (void)result;
#endif

    src_store.close();
    dst_store.close();
    fs::remove_all(base);
}

// When file permissions prevent access during a scan, the system SHALL skip
// the file and continue with remaining files.
RC_GTEST_PROP(ErrorHandlingProperty, PermissionErrorHandling, ()) {
    auto base = make_temp_dir();
    auto root = base / "drive";
    fs::create_directories(root);

    write_file(root / "readable.txt", "hello");
    write_file(root / "noperm.txt", "secret");

#ifndef _WIN32
    ::chmod((root / "noperm.txt").c_str(), 0000);
#endif

    auto store = ManifestStore::open(base / "perm.db");
    std::vector<ScanError> errors;

    MerkleNode tree;
    ASSERT_NO_THROW(tree = MerkleEngine::build_tree(root, store, errors));

#ifndef _WIN32
    bool found_readable = false;
    for (const auto& child : tree.children) {
        if (child.path == "readable.txt" && !child.hash.empty())
            found_readable = true;
    }
    RC_ASSERT(found_readable);
    RC_ASSERT(!errors.empty());

    ::chmod((root / "noperm.txt").c_str(), 0644);
#else
    (void)tree;
#endif

    store.close();
    fs::remove_all(base);
}

// If database corruption is detected, the system SHALL rebuild the database
// from scratch, restoring a functional empty schema.
RC_GTEST_PROP(ErrorHandlingProperty, DatabaseCorruptionRecovery, ()) {
    auto base = make_temp_dir();
    auto db_path = base / "corrupt.db";

    {
        auto store = ManifestStore::open(db_path);
        store.register_drive("drive_a");
        store.close();
    }

    // Overwrite the database file with garbage to simulate corruption.
    {
        std::ofstream f(db_path, std::ios::binary | std::ios::trunc);
        f << "THIS IS NOT A VALID SQLITE DATABASE - CORRUPTED";
    }

    try {
        auto store = ManifestStore::open(db_path);
        store.get_all_drives();
        store.close();
    } catch (...) {
        // Expected on a corrupt database.
    }

    ManifestStore store;
    bool rebuilt = false;
    try {
        store = ManifestStore::open(db_path);
        rebuilt = true;
    } catch (...) {
        rebuilt = store.rebuild_from_filesystem(db_path);
    }

    RC_ASSERT(rebuilt);

    store.register_drive("drive_b");
    auto drives = store.get_all_drives();
    bool found = std::find(drives.begin(), drives.end(), "drive_b") != drives.end();
    RC_ASSERT(found);

    store.close();
    fs::remove_all(base);
}
