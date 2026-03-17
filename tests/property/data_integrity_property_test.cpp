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

namespace fs = std::filesystem;
using namespace caravault;

namespace {

fs::path make_temp_dir() {
    static std::atomic<int> counter{0};
    fs::path p = fs::temp_directory_path() /
                 ("caravault_integrity_prop_" + std::to_string(counter.fetch_add(1)));
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

// For any file read operation, if the computed SHA-256 hash does not match
// the stored hash, the system SHALL detect the mismatch.
RC_GTEST_PROP(DataIntegrityProperty, HashVerificationOnRead, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto good_content = *gen_content();
    auto corrupt_content = *rc::gen::map(
        gen_content(), [&](std::string s) { return s == good_content ? s + "_corrupted" : s; });

    TempDrive drv(base, "drive_a");
    fs::path abs_path = drv.root / filename;

    write_file(abs_path, good_content);
    std::string real_hash = MerkleEngine::compute_file_hash(abs_path);

    VersionVector vv;
    vv.increment("drive_a");
    drv.store.upsert_file(make_meta(filename, real_hash, good_content.size(), vv));

    RC_ASSERT(Executor::read_file_verified(filename, abs_path, drv.store));
    RC_ASSERT(!drv.store.get_file(filename)->corrupted);

    write_file(abs_path, corrupt_content);
    RC_ASSERT(!Executor::read_file_verified(filename, abs_path, drv.store));

    fs::remove_all(base);
}

// For any file with a hash mismatch, the system SHALL mark the file as corrupted.
RC_GTEST_PROP(DataIntegrityProperty, CorruptionMarking, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto good_content = *gen_content();
    auto corrupt_content = *rc::gen::map(
        gen_content(), [&](std::string s) { return s == good_content ? s + "_corrupted" : s; });

    TempDrive drv(base, "drive_a");
    fs::path abs_path = drv.root / filename;

    write_file(abs_path, good_content);
    std::string real_hash = MerkleEngine::compute_file_hash(abs_path);

    VersionVector vv;
    vv.increment("drive_a");
    drv.store.upsert_file(make_meta(filename, real_hash, good_content.size(), vv));

    write_file(abs_path, corrupt_content);
    RC_ASSERT(!Executor::read_file_verified(filename, abs_path, drv.store));

    auto meta = drv.store.get_file(filename);
    RC_ASSERT(meta.has_value());
    RC_ASSERT(meta->corrupted);

    fs::remove_all(base);
}

// For any corrupted file, if an uncorrupted replica exists on another drive,
// the system SHALL restore from that replica.
RC_GTEST_PROP(DataIntegrityProperty, CorruptionRecoveryFromReplica, ()) {
    auto base = make_temp_dir();
    auto filename = *gen_filename();
    auto good_content = *gen_content();
    auto corrupt_content = *rc::gen::map(
        gen_content(), [&](std::string s) { return s == good_content ? s + "_corrupted" : s; });

    TempDrive src(base, "drive_src");
    TempDrive dst(base, "drive_dst");

    fs::path src_abs = src.root / filename;
    write_file(src_abs, good_content);
    std::string good_hash = MerkleEngine::compute_file_hash(src_abs);

    VersionVector vv;
    vv.increment("drive_src");
    src.store.upsert_file(make_meta(filename, good_hash, good_content.size(), vv));

    // Manifest records the correct hash; on-disk content is silently corrupted.
    fs::path dst_abs = dst.root / filename;
    write_file(dst_abs, corrupt_content);
    dst.store.upsert_file(make_meta(filename, good_hash, good_content.size(), vv));

    RC_ASSERT(!Executor::read_file_verified(filename, dst_abs, dst.store));
    RC_ASSERT(dst.store.get_file(filename)->corrupted);

    std::map<std::string, fs::path> roots{{"drive_src", src.root}, {"drive_dst", dst.root}};
    std::map<std::string, ManifestStore*> manifests{{"drive_src", &src.store},
                                                    {"drive_dst", &dst.store}};

    RC_ASSERT(Executor{}.restore_from_replica(filename, "drive_dst", roots, manifests));
    RC_ASSERT(Executor::verify_hash(dst_abs, good_hash));
    RC_ASSERT(!dst.store.get_file(filename)->corrupted);

    fs::remove_all(base);
}
