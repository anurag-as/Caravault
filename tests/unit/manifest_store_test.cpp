#include "manifest_store.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;
using namespace caravault;

// ---------------------------------------------------------------------------
// Test fixture: creates a temp DB file and removes it after each test
// ---------------------------------------------------------------------------

class ManifestStoreTest : public ::testing::Test {
protected:
    fs::path db_path_;
    ManifestStore store_;

    void SetUp() override {
        db_path_ = fs::temp_directory_path() / "caravault_test.db";
        fs::remove(db_path_);
        store_ = ManifestStore::open(db_path_);
    }

    void TearDown() override {
        store_.close();
        fs::remove(db_path_);
    }
};

// ---------------------------------------------------------------------------
// Drive operations
// ---------------------------------------------------------------------------

TEST_F(ManifestStoreTest, RegisterDriveAndGetAllDrives) {
    store_.register_drive("drive-A");
    store_.register_drive("drive-B");

    auto drives = store_.get_all_drives();
    ASSERT_EQ(drives.size(), 2u);
    // Order not guaranteed; check both are present
    bool found_a = false, found_b = false;
    for (const auto& d : drives) {
        if (d == "drive-A") found_a = true;
        if (d == "drive-B") found_b = true;
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);
}

TEST_F(ManifestStoreTest, RegisterDriveIsIdempotent) {
    store_.register_drive("drive-A");
    store_.register_drive("drive-A"); // should not throw or duplicate
    EXPECT_EQ(store_.get_all_drives().size(), 1u);
}

TEST_F(ManifestStoreTest, UpdateLastSeenDoesNotThrow) {
    store_.register_drive("drive-A");
    EXPECT_NO_THROW(store_.update_last_seen("drive-A"));
}

TEST_F(ManifestStoreTest, GetAllDrivesEmptyInitially) {
    EXPECT_TRUE(store_.get_all_drives().empty());
}

// ---------------------------------------------------------------------------
// File metadata operations
// ---------------------------------------------------------------------------

static FileMetadata make_file(const std::string& path) {
    FileMetadata m;
    m.path = path;
    m.hash = "abc123";
    m.size = 1024;
    m.mtime = 1700000000;
    m.version_vector = VersionVector({{"drive-A", 1}});
    m.tombstone = false;
    m.inode = 42;
    return m;
}

TEST_F(ManifestStoreTest, UpsertAndGetFile) {
    FileMetadata original = make_file("docs/readme.txt");
    store_.upsert_file(original);

    auto result = store_.get_file("docs/readme.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->path, original.path);
    EXPECT_EQ(result->hash, original.hash);
    EXPECT_EQ(result->size, original.size);
    EXPECT_EQ(result->mtime, original.mtime);
    EXPECT_EQ(result->version_vector, original.version_vector);
    EXPECT_EQ(result->tombstone, original.tombstone);
    EXPECT_EQ(result->inode, original.inode);
}

TEST_F(ManifestStoreTest, GetFileMissingReturnsNullopt) {
    EXPECT_FALSE(store_.get_file("nonexistent.txt").has_value());
}

TEST_F(ManifestStoreTest, UpsertFileUpdatesExisting) {
    FileMetadata m = make_file("file.txt");
    store_.upsert_file(m);

    m.hash = "newHash";
    m.size = 2048;
    store_.upsert_file(m);

    auto result = store_.get_file("file.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->hash, "newHash");
    EXPECT_EQ(result->size, 2048u);
}

TEST_F(ManifestStoreTest, GetAllFilesReturnsAll) {
    store_.upsert_file(make_file("a.txt"));
    store_.upsert_file(make_file("b.txt"));
    store_.upsert_file(make_file("c.txt"));

    auto files = store_.get_all_files();
    EXPECT_EQ(files.size(), 3u);
}

TEST_F(ManifestStoreTest, DeleteFileRemovesRecord) {
    store_.upsert_file(make_file("file.txt"));
    store_.delete_file("file.txt");
    EXPECT_FALSE(store_.get_file("file.txt").has_value());
}

TEST_F(ManifestStoreTest, FileWithoutInodeRoundTrip) {
    FileMetadata m = make_file("no_inode.txt");
    m.inode = std::nullopt;
    store_.upsert_file(m);

    auto result = store_.get_file("no_inode.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->inode.has_value());
}

TEST_F(ManifestStoreTest, TombstoneRoundTrip) {
    FileMetadata m = make_file("deleted.txt");
    m.tombstone = true;
    store_.upsert_file(m);

    auto result = store_.get_file("deleted.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->tombstone);
}

TEST_F(ManifestStoreTest, FileMetadataRoundTrip) {
    FileMetadata original = make_file("roundtrip.txt");
    store_.upsert_file(original);
    auto result = store_.get_file("roundtrip.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, original);
}

// ---------------------------------------------------------------------------
// Merkle tree operations
// ---------------------------------------------------------------------------

TEST_F(ManifestStoreTest, UpsertAndGetMerkleHash) {
    store_.upsert_merkle_node("docs/", "hashABC", 1);
    auto h = store_.get_merkle_hash("docs/", 1);
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(*h, "hashABC");
}

TEST_F(ManifestStoreTest, GetMerkleHashMissingReturnsNullopt) {
    EXPECT_FALSE(store_.get_merkle_hash("missing/", 0).has_value());
}

TEST_F(ManifestStoreTest, UpsertMerkleNodeUpdatesHash) {
    store_.upsert_merkle_node("root/", "oldHash", 2);
    store_.upsert_merkle_node("root/", "newHash", 2);
    auto h = store_.get_merkle_hash("root/", 2);
    ASSERT_TRUE(h.has_value());
    EXPECT_EQ(*h, "newHash");
}

TEST_F(ManifestStoreTest, MerkleNodeLevelDistinguishesEntries) {
    store_.upsert_merkle_node("path/", "hashLevel0", 0);
    store_.upsert_merkle_node("path/", "hashLevel1", 1);
    EXPECT_EQ(*store_.get_merkle_hash("path/", 0), "hashLevel0");
    EXPECT_EQ(*store_.get_merkle_hash("path/", 1), "hashLevel1");
}

// ---------------------------------------------------------------------------
// Transaction log operations
// ---------------------------------------------------------------------------

TEST_F(ManifestStoreTest, BeginAndCompleteOperation) {
    uint64_t id = store_.begin_operation("COPY", "file.txt");
    EXPECT_GT(id, 0u);

    auto pending = store_.get_incomplete_operations();
    ASSERT_EQ(pending.size(), 1u);
    EXPECT_EQ(pending[0].id, id);
    EXPECT_EQ(pending[0].operation, "COPY");
    EXPECT_EQ(pending[0].path, "file.txt");

    store_.complete_operation(id);
    EXPECT_TRUE(store_.get_incomplete_operations().empty());
}

TEST_F(ManifestStoreTest, MultipleIncompleteOperations) {
    store_.begin_operation("COPY", "a.txt");
    store_.begin_operation("DELETE", "b.txt");
    uint64_t id3 = store_.begin_operation("RENAME", "c.txt");

    store_.complete_operation(id3);

    auto pending = store_.get_incomplete_operations();
    EXPECT_EQ(pending.size(), 2u);
}

TEST_F(ManifestStoreTest, GetIncompleteOperationsEmptyInitially) {
    EXPECT_TRUE(store_.get_incomplete_operations().empty());
}

// ---------------------------------------------------------------------------
// Transaction atomicity
// ---------------------------------------------------------------------------

TEST_F(ManifestStoreTest, CommitPersistsData) {
    store_.begin_transaction();
    store_.upsert_file(make_file("tx_file.txt"));
    store_.commit();

    EXPECT_TRUE(store_.get_file("tx_file.txt").has_value());
}

TEST_F(ManifestStoreTest, RollbackDiscardsData) {
    store_.begin_transaction();
    store_.upsert_file(make_file("rollback_file.txt"));
    store_.rollback();

    EXPECT_FALSE(store_.get_file("rollback_file.txt").has_value());
}
