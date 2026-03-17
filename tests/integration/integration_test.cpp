#include "cdc_chunker.hpp"
#include "conflict_resolver.hpp"
#include "executor.hpp"
#include "manifest_store.hpp"
#include "merkle_engine.hpp"
#include "platform.hpp"
#include "sync_planner.hpp"
#include "version_vector.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

void write_file(const fs::path& abs_path, const std::string& content) {
    fs::create_directories(abs_path.parent_path());
    std::ofstream f(abs_path, std::ios::binary | std::ios::trunc);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string read_file(const fs::path& abs_path) {
    std::ifstream f(abs_path, std::ios::binary);
    if (!f.is_open())
        return "";
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

FileMetadata make_meta_from_disk(const std::string& rel_path,
                                 const fs::path& drive_root,
                                 const std::string& drive_id) {
    fs::path abs = drive_root / rel_path;
    FileMetadata m;
    m.path = rel_path;
    m.hash = MerkleEngine::compute_file_hash(abs);
    m.size = static_cast<uint64_t>(fs::file_size(abs));
    m.mtime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                        fs::last_write_time(abs).time_since_epoch())
                                        .count());
    m.version_vector = VersionVector({{drive_id, 1}});
    m.tombstone = false;
    return m;
}

}  // namespace

class IntegrationTestBase : public ::testing::Test {
protected:
    fs::path temp_dir_;
    fs::path drive_a_root_;
    fs::path drive_b_root_;
    fs::path drive_c_root_;
    ManifestStore store_a_;
    ManifestStore store_b_;
    ManifestStore store_c_;

    void SetUp() override {
        std::string tmpl = (fs::temp_directory_path() / "caravault_it_XXXXXX").string();
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back('\0');
#ifdef _WIN32
        temp_dir_ = fs::temp_directory_path() /
                    ("caravault_it_" +
                     std::to_string(
                         std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(temp_dir_);
#else
        char* result = ::mkdtemp(buf.data());
        ASSERT_NE(result, nullptr) << "mkdtemp failed";
        temp_dir_ = fs::path(result);
#endif
        drive_a_root_ = temp_dir_ / "drive_a";
        drive_b_root_ = temp_dir_ / "drive_b";
        drive_c_root_ = temp_dir_ / "drive_c";

        for (auto& d : {drive_a_root_, drive_b_root_, drive_c_root_})
            fs::create_directories(d / ".caravault");

        store_a_ = ManifestStore::open(drive_a_root_ / ".caravault" / "manifest.db");
        store_b_ = ManifestStore::open(drive_b_root_ / ".caravault" / "manifest.db");
        store_c_ = ManifestStore::open(drive_c_root_ / ".caravault" / "manifest.db");

        store_a_.register_drive("drive_a");
        store_b_.register_drive("drive_b");
        store_c_.register_drive("drive_c");
    }

    void TearDown() override {
        store_a_.close();
        store_b_.close();
        store_c_.close();
        std::error_code ec;
        fs::remove_all(temp_dir_, ec);
    }

    void run_full_sync(const std::map<std::string, fs::path>& roots,
                       std::map<std::string, ManifestStore*>& manifests) {
        ConflictResolver resolver;
        SyncPlanner planner;
        Executor executor;

        auto conflicts = resolver.detect_conflicts(manifests);
        std::vector<Resolution> resolutions;
        resolutions.reserve(conflicts.size());
        for (const auto& c : conflicts)
            resolutions.push_back(resolver.resolve(c, manifests.size()));

        auto ops = planner.plan_sync(manifests, resolutions);
        for (const auto& op : ops)
            executor.execute(op, roots, manifests);
    }
};

// After syncing three drives with disjoint files, all drives SHALL contain all
// files with matching content. A second sync SHALL produce no new operations.
TEST_F(IntegrationTestBase, ThreeDriveNoConflictSyncReachesConsistentState) {
    write_file(drive_a_root_ / "docs/readme.txt", "Hello from A");
    write_file(drive_a_root_ / "photos/img1.jpg", "JPEG data A");
    write_file(drive_b_root_ / "music/song.mp3", "MP3 data B");

    store_a_.upsert_file(make_meta_from_disk("docs/readme.txt", drive_a_root_, "drive_a"));
    store_a_.upsert_file(make_meta_from_disk("photos/img1.jpg", drive_a_root_, "drive_a"));
    store_b_.upsert_file(make_meta_from_disk("music/song.mp3", drive_b_root_, "drive_b"));

    std::map<std::string, fs::path> roots = {
        {"drive_a", drive_a_root_},
        {"drive_b", drive_b_root_},
        {"drive_c", drive_c_root_},
    };
    std::map<std::string, ManifestStore*> manifests = {
        {"drive_a", &store_a_},
        {"drive_b", &store_b_},
        {"drive_c", &store_c_},
    };

    run_full_sync(roots, manifests);

    for (const auto& [drive_id, root] : roots) {
        EXPECT_TRUE(fs::exists(root / "docs/readme.txt")) << drive_id << " missing docs/readme.txt";
        EXPECT_TRUE(fs::exists(root / "photos/img1.jpg")) << drive_id << " missing photos/img1.jpg";
        EXPECT_TRUE(fs::exists(root / "music/song.mp3")) << drive_id << " missing music/song.mp3";
        EXPECT_EQ(read_file(root / "docs/readme.txt"), "Hello from A");
        EXPECT_EQ(read_file(root / "photos/img1.jpg"), "JPEG data A");
        EXPECT_EQ(read_file(root / "music/song.mp3"), "MP3 data B");
    }

    for (auto& [drive_id, store_ptr] : manifests) {
        auto readme = store_ptr->get_file("docs/readme.txt");
        ASSERT_TRUE(readme.has_value()) << drive_id << " manifest missing docs/readme.txt";
        EXPECT_GT(readme->version_vector.get_clock("drive_a"), 0u);

        auto song = store_ptr->get_file("music/song.mp3");
        ASSERT_TRUE(song.has_value()) << drive_id << " manifest missing music/song.mp3";
        EXPECT_GT(song->version_vector.get_clock("drive_b"), 0u);
    }

    // Second sync must produce no COPY/REPLACE/DELETE/RENAME operations.
    auto conflicts2 = ConflictResolver{}.detect_conflicts(manifests);
    std::vector<Resolution> resolutions2;
    for (const auto& c : conflicts2)
        resolutions2.push_back(ConflictResolver{}.resolve(c, manifests.size()));

    auto ops2 = SyncPlanner{}.plan_sync(manifests, resolutions2);
    size_t non_mkdir = 0;
    for (const auto& op : ops2)
        if (op.type != SyncOpType::MKDIR)
            ++non_mkdir;
    EXPECT_EQ(non_mkdir, 0u) << "Second sync should produce no file transfer operations";
}

// When 2 of 3 drives agree on a file version, the system SHALL resolve the
// conflict using MAJORITY_QUORUM and select one of the agreeing drives as winner.
TEST_F(IntegrationTestBase, QuorumResolvesConflict) {
    write_file(drive_a_root_ / "shared.txt", "Version A");
    store_a_.upsert_file(make_meta("shared.txt",
                                   MerkleEngine::compute_file_hash(drive_a_root_ / "shared.txt"),
                                   9, 1700000001, VersionVector({{"drive_a", 1}})));

    write_file(drive_b_root_ / "shared.txt", "Version B");
    std::string hash_b = MerkleEngine::compute_file_hash(drive_b_root_ / "shared.txt");
    store_b_.upsert_file(
        make_meta("shared.txt", hash_b, 9, 1700000002, VersionVector({{"drive_b", 1}})));

    // drive_c has the same content as drive_b, forming a quorum
    write_file(drive_c_root_ / "shared.txt", "Version B");
    store_c_.upsert_file(
        make_meta("shared.txt", hash_b, 9, 1700000003, VersionVector({{"drive_c", 1}})));

    std::map<std::string, ManifestStore*> manifests = {
        {"drive_a", &store_a_},
        {"drive_b", &store_b_},
        {"drive_c", &store_c_},
    };

    ConflictResolver resolver;
    auto conflicts = resolver.detect_conflicts(manifests);

    ASSERT_FALSE(conflicts.empty()) << "Expected shared.txt to be detected as a conflict";
    auto it = std::find_if(conflicts.begin(), conflicts.end(),
                           [](const ConflictInfo& c) { return c.path == "shared.txt"; });
    ASSERT_NE(it, conflicts.end()) << "shared.txt should be in the conflict list";

    Resolution res = resolver.resolve(*it, manifests.size());

    EXPECT_EQ(res.strategy_used, ResolutionStrategy::MAJORITY_QUORUM);
    EXPECT_TRUE(res.winning_drive_id == "drive_b" || res.winning_drive_id == "drive_c")
        << "Winner should be drive_b or drive_c (quorum side), got: " << res.winning_drive_id;
}

// When a valid temp file exists for an incomplete operation, recover SHALL
// promote it to the target path and clear the transaction log.
TEST_F(IntegrationTestBase, RecoverIncompleteOperationPromotesTempFile) {
    const std::string content = "Critical data";
    write_file(drive_a_root_ / "important.txt", content);
    std::string expected_hash =
        MerkleEngine::compute_file_hash(drive_a_root_ / "important.txt");

    store_b_.upsert_file(make_meta("important.txt", expected_hash,
                                   static_cast<uint64_t>(content.size()), 1700000010,
                                   VersionVector({{"drive_a", 1}})));

    // Simulate crash: log the operation but never complete it
    store_b_.begin_operation("WRITE", (drive_b_root_ / "important.txt").string());

    // Write a valid temp file with the correct content
    write_file(drive_b_root_ / "important.txt.caravault.tmp", content);

    Executor{}.recover_incomplete_operations(store_b_, drive_b_root_);

    EXPECT_TRUE(fs::exists(drive_b_root_ / "important.txt"))
        << "important.txt should exist after recovery";
    EXPECT_TRUE(store_b_.get_incomplete_operations().empty())
        << "Transaction log should be clean after recovery";
    EXPECT_EQ(read_file(drive_b_root_ / "important.txt"), content);
}

// When a temp file has a hash mismatch, recover SHALL delete it and leave no
// incomplete operations in the transaction log.
TEST_F(IntegrationTestBase, RecoverDeletesCorruptedTempFile) {
    store_b_.upsert_file(
        make_meta("data.txt",
                  "0000000000000000000000000000000000000000000000000000000000000000",
                  10, 1700000020, VersionVector({{"drive_a", 1}})));

    store_b_.begin_operation("WRITE", (drive_b_root_ / "data.txt").string());
    write_file(drive_b_root_ / "data.txt.caravault.tmp", "CORRUPTED CONTENT THAT DOES NOT MATCH");

    Executor{}.recover_incomplete_operations(store_b_, drive_b_root_);

    EXPECT_FALSE(fs::exists(drive_b_root_ / "data.txt.caravault.tmp"))
        << "Corrupted temp file should be removed";
    EXPECT_TRUE(store_b_.get_incomplete_operations().empty());
}

// When a file is renamed on one drive and modified on another, the system SHALL
// detect a conflict on the original path and complete planning without crashing.
TEST_F(IntegrationTestBase, RenameOnOneDriveConflictWithModifyOnAnother) {
    const std::string original_content = "Original content";
    write_file(drive_a_root_ / "original.txt", original_content);
    write_file(drive_b_root_ / "original.txt", original_content);

    std::string original_hash =
        MerkleEngine::compute_file_hash(drive_a_root_ / "original.txt");
    VersionVector shared_vv({{"drive_a", 1}, {"drive_b", 1}});

    auto shared_meta = make_meta("original.txt", original_hash,
                                 static_cast<uint64_t>(original_content.size()),
                                 1700000030, shared_vv);
    store_a_.upsert_file(shared_meta);
    store_b_.upsert_file(shared_meta);

    // drive_a: rename original.txt -> renamed.txt
    write_file(drive_a_root_ / "renamed.txt", original_content);
    fs::remove(drive_a_root_ / "original.txt");
    store_a_.mark_deleted("original.txt", "drive_a");
    store_a_.upsert_file(make_meta("renamed.txt", original_hash,
                                   static_cast<uint64_t>(original_content.size()),
                                   1700000031, VersionVector({{"drive_a", 2}})));

    // drive_b: modify original.txt
    const std::string modified_content = "Modified content";
    write_file(drive_b_root_ / "original.txt", modified_content);
    store_b_.upsert_file(
        make_meta("original.txt",
                  MerkleEngine::compute_file_hash(drive_b_root_ / "original.txt"),
                  static_cast<uint64_t>(modified_content.size()), 1700000032,
                  VersionVector({{"drive_b", 2}})));

    std::map<std::string, ManifestStore*> manifests = {
        {"drive_a", &store_a_},
        {"drive_b", &store_b_},
    };
    std::map<std::string, fs::path> roots = {
        {"drive_a", drive_a_root_},
        {"drive_b", drive_b_root_},
    };

    auto conflicts = ConflictResolver{}.detect_conflicts(manifests);
    bool original_conflict_found = std::any_of(conflicts.begin(), conflicts.end(),
                                               [](const ConflictInfo& c) {
                                                   return c.path == "original.txt";
                                               });
    EXPECT_TRUE(original_conflict_found) << "original.txt should be detected as a conflict";

    std::vector<Resolution> resolutions;
    for (const auto& c : conflicts)
        resolutions.push_back(ConflictResolver{}.resolve(c, manifests.size()));

    EXPECT_NO_THROW({
        auto ops = SyncPlanner{}.plan_sync(manifests, resolutions);
        (void)ops;
    });

    EXPECT_NO_THROW({ run_full_sync(roots, manifests); });
}

// A file larger than kLargeFileThreshold SHALL produce multiple CDC chunks.
// After sync the file on the target drive SHALL have a matching hash.
// Modifying only part of the file SHALL produce fewer changed chunks than total chunks.
TEST_F(IntegrationTestBase, LargeFileChunkedSync) {
    const size_t file_size = 2 * 1024 * 1024;
    std::string large_data(file_size, 'X');
    write_file(drive_a_root_ / "bigfile.bin", large_data);
    MerkleEngine::build_tree(drive_a_root_, store_a_);

    std::map<std::string, fs::path> roots = {
        {"drive_a", drive_a_root_},
        {"drive_b", drive_b_root_},
    };
    std::map<std::string, ManifestStore*> manifests = {
        {"drive_a", &store_a_},
        {"drive_b", &store_b_},
    };

    CDCChunker chunker;
    auto original_chunks = chunker.chunk_file(drive_a_root_ / "bigfile.bin");
    EXPECT_GT(original_chunks.size(), 1u)
        << "A 2 MB file should produce multiple CDC chunks (threshold is 1 MB)";

    run_full_sync(roots, manifests);

    ASSERT_TRUE(fs::exists(drive_b_root_ / "bigfile.bin"))
        << "bigfile.bin should exist on drive_b after sync";
    EXPECT_EQ(MerkleEngine::compute_file_hash(drive_a_root_ / "bigfile.bin"),
              MerkleEngine::compute_file_hash(drive_b_root_ / "bigfile.bin"))
        << "File hashes should match between drives after sync";

    // Modify bytes 512KB-768KB and verify CDC isolates the change
    std::string modified_data = large_data;
    for (size_t i = 512 * 1024; i < 768 * 1024; ++i)
        modified_data[i] = 'Y';
    write_file(drive_a_root_ / "bigfile.bin", modified_data);

    auto new_chunks = chunker.chunk_file(drive_a_root_ / "bigfile.bin");
    auto changed_chunks = CDCChunker::diff_chunks(original_chunks, new_chunks);

    EXPECT_GT(changed_chunks.size(), 0u) << "Some chunks should have changed";
    EXPECT_LT(changed_chunks.size(), new_chunks.size())
        << "Not all chunks should have changed (CDC should isolate the modification)";
}

// normalize_path SHALL convert backslashes to forward slashes and be idempotent.
// Paths stored in the manifest SHALL use forward slashes.
// get_platform_name SHALL return a known platform string.
// File permissions SHALL be preserved after sync.
TEST_F(IntegrationTestBase, CrossPlatformPathNormalizationAndPermissions) {
    EXPECT_EQ(normalize_path("docs\\readme.txt"), "docs/readme.txt");
    EXPECT_EQ(normalize_path("a\\b\\c"), "a/b/c");

    std::string p = "docs/readme.txt";
    EXPECT_EQ(normalize_path(normalize_path(p)), normalize_path(p));

    // Paths stored in the manifest must not contain backslashes
    write_file(drive_a_root_ / "subdir" / "file.txt", "hello");
    store_a_.upsert_file(
        make_meta(normalize_path("subdir/file.txt"),
                  MerkleEngine::compute_file_hash(drive_a_root_ / "subdir" / "file.txt"),
                  5, 1700000040, VersionVector({{"drive_a", 1}})));

    auto retrieved = store_a_.get_file("subdir/file.txt");
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->path.find('\\'), std::string::npos)
        << "Stored path should use forward slashes";

    std::string platform = get_platform_name();
    EXPECT_TRUE(platform == "linux" || platform == "macos" || platform == "windows")
        << "Unexpected platform name: " << platform;

    // Permissions should be preserved during sync
    write_file(drive_a_root_ / "perms_test.txt", "permission test");
    FilePermissions perms644;
    perms644.owner_read = true;
    perms644.owner_write = true;
    perms644.group_read = true;
    perms644.other_read = true;
    perms644.raw_mode = 0644;
    set_permissions(drive_a_root_ / "perms_test.txt", perms644);

    MerkleEngine::build_tree(drive_a_root_, store_a_);

    std::map<std::string, fs::path> roots = {
        {"drive_a", drive_a_root_},
        {"drive_b", drive_b_root_},
    };
    std::map<std::string, ManifestStore*> manifests = {
        {"drive_a", &store_a_},
        {"drive_b", &store_b_},
    };
    run_full_sync(roots, manifests);

    ASSERT_TRUE(fs::exists(drive_b_root_ / "perms_test.txt"));
    EXPECT_TRUE(get_permissions(drive_b_root_ / "perms_test.txt").owner_read)
        << "Synced file should be owner-readable";

    // A manifest entry stored with a normalized path must be retrievable by that path
    std::string mixed_path = normalize_path("docs\\notes\\todo.txt");
    store_a_.upsert_file(
        make_meta(mixed_path,
                  "abcdef1234567890abcdef1234567890abcdef1234567890abcdef1234567890",
                  100, 1700000050, VersionVector({{"drive_a", 1}})));

    auto retrieved2 = store_a_.get_file(mixed_path);
    ASSERT_TRUE(retrieved2.has_value())
        << "Should be able to retrieve file by normalized path";
    EXPECT_EQ(retrieved2->path, mixed_path);
}
