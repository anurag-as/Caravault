#include "manifest_store.hpp"
#include "merkle_engine.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

struct TempTree {
    fs::path root;

    explicit TempTree(const std::string& name) {
        root = fs::temp_directory_path() / ("caravault_tombstone_test_" + name);
        fs::remove_all(root);
        fs::create_directories(root);
    }

    ~TempTree() { fs::remove_all(root); }

    fs::path write(const std::string& rel, const std::string& content = "data") {
        fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        return p;
    }

    void remove(const std::string& rel) { fs::remove(root / rel); }

    ManifestStore open_store(const std::string& tag) {
        fs::path db = fs::temp_directory_path() / ("caravault_tombstone_manifest_" + tag + ".db");
        fs::remove(db);
        fs::remove(fs::path(db.string() + "-wal"));
        fs::remove(fs::path(db.string() + "-shm"));
        return ManifestStore::open(db);
    }
};

// Simulates the tombstone-on-scan logic added to do_scan:
// after build_tree, any non-tombstone manifest entry not visited on disk
// gets mark_deleted() called on it.
void scan_with_tombstone(const fs::path& root, ManifestStore& store, const std::string& drive_id) {
    std::vector<ScanError> errors;
    MerkleNode tree = MerkleEngine::build_tree(root, store, errors);

    std::vector<std::string> visited;
    MerkleEngine::collect_leaves(tree, visited);
    std::unordered_set<std::string> visited_set(visited.begin(), visited.end());

    for (const auto& f : store.get_all_files()) {
        if (!f.tombstone && visited_set.find(f.path) == visited_set.end()) {
            store.mark_deleted(f.path, drive_id);
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Deleted file gets tombstoned on re-scan
// ---------------------------------------------------------------------------

TEST(TombstoneScanTest, DeletedFileIsTombstonedOnRescan) {
    TempTree t("deleted_file");
    t.write("keep.txt", "stays");
    t.write("gone.txt", "will be deleted");

    auto store = t.open_store("deleted_file");
    scan_with_tombstone(t.root, store, "driveA");

    // Both files present in manifest, neither tombstoned
    auto keep = store.get_file("keep.txt");
    auto gone = store.get_file("gone.txt");
    ASSERT_TRUE(keep.has_value());
    ASSERT_TRUE(gone.has_value());
    EXPECT_FALSE(keep->tombstone);
    EXPECT_FALSE(gone->tombstone);

    // Delete gone.txt from disk and re-scan
    t.remove("gone.txt");
    scan_with_tombstone(t.root, store, "driveA");

    keep = store.get_file("keep.txt");
    gone = store.get_file("gone.txt");
    ASSERT_TRUE(keep.has_value());
    ASSERT_TRUE(gone.has_value());
    EXPECT_FALSE(keep->tombstone) << "keep.txt should still be live";
    EXPECT_TRUE(gone->tombstone) << "gone.txt must be tombstoned after deletion from disk";
}

// ---------------------------------------------------------------------------
// File that was never on disk is not affected (no phantom tombstones)
// ---------------------------------------------------------------------------

TEST(TombstoneScanTest, OnlyPresentFilesAreTombstoned) {
    TempTree t("no_phantom");
    t.write("a.txt", "alpha");
    t.write("b.txt", "beta");

    auto store = t.open_store("no_phantom");
    scan_with_tombstone(t.root, store, "driveA");

    // Remove a.txt
    t.remove("a.txt");
    scan_with_tombstone(t.root, store, "driveA");

    auto a = store.get_file("a.txt");
    auto b = store.get_file("b.txt");
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_TRUE(a->tombstone);
    EXPECT_FALSE(b->tombstone) << "b.txt was not deleted and must not be tombstoned";
}

// ---------------------------------------------------------------------------
// Already-tombstoned entry is not double-tombstoned (version vector stable)
// ---------------------------------------------------------------------------

TEST(TombstoneScanTest, AlreadyTombstonedEntryNotModifiedAgain) {
    TempTree t("double_tombstone");
    t.write("file.txt", "content");

    auto store = t.open_store("double_tombstone");
    scan_with_tombstone(t.root, store, "driveA");

    t.remove("file.txt");
    scan_with_tombstone(t.root, store, "driveA");

    auto after_first = store.get_file("file.txt");
    ASSERT_TRUE(after_first.has_value());
    EXPECT_TRUE(after_first->tombstone);
    auto vv_after_first = after_first->version_vector;

    // Re-scan again — file still absent, already tombstoned
    scan_with_tombstone(t.root, store, "driveA");

    auto after_second = store.get_file("file.txt");
    ASSERT_TRUE(after_second.has_value());
    EXPECT_TRUE(after_second->tombstone);
    // Version vector must not be incremented again for an already-tombstoned entry
    EXPECT_EQ(after_second->version_vector, vv_after_first)
        << "Re-scanning an already-tombstoned absent file must not bump the version vector";
}

// ---------------------------------------------------------------------------
// Multiple files deleted at once — all get tombstoned
// ---------------------------------------------------------------------------

TEST(TombstoneScanTest, MultipleDeletedFilesAllTombstoned) {
    TempTree t("multi_delete");
    t.write("one.txt", "1");
    t.write("two.txt", "2");
    t.write("three.txt", "3");
    t.write("keep.txt", "keep");

    auto store = t.open_store("multi_delete");
    scan_with_tombstone(t.root, store, "driveA");

    t.remove("one.txt");
    t.remove("two.txt");
    t.remove("three.txt");
    scan_with_tombstone(t.root, store, "driveA");

    EXPECT_TRUE(store.get_file("one.txt")->tombstone);
    EXPECT_TRUE(store.get_file("two.txt")->tombstone);
    EXPECT_TRUE(store.get_file("three.txt")->tombstone);
    EXPECT_FALSE(store.get_file("keep.txt")->tombstone);
}

// ---------------------------------------------------------------------------
// Deleted file in a subdirectory is tombstoned
// ---------------------------------------------------------------------------

TEST(TombstoneScanTest, DeletedFileInSubdirIsTombstoned) {
    TempTree t("subdir_delete");
    t.write("docs/spec.md", "spec content");
    t.write("docs/notes.md", "notes content");
    t.write("readme.txt", "root file");

    auto store = t.open_store("subdir_delete");
    scan_with_tombstone(t.root, store, "driveA");

    t.remove("docs/spec.md");
    scan_with_tombstone(t.root, store, "driveA");

    EXPECT_TRUE(store.get_file("docs/spec.md")->tombstone);
    EXPECT_FALSE(store.get_file("docs/notes.md")->tombstone);
    EXPECT_FALSE(store.get_file("readme.txt")->tombstone);
}

// ---------------------------------------------------------------------------
// collect_leaves returns all visible leaf paths
// ---------------------------------------------------------------------------

TEST(TombstoneScanTest, CollectLeavesReturnsAllVisibleFiles) {
    TempTree t("collect_leaves");
    t.write("a.txt", "a");
    t.write("sub/b.txt", "b");
    t.write("sub/deep/c.txt", "c");
    t.write(".hidden", "skip");

    auto store = t.open_store("collect_leaves");
    std::vector<ScanError> errors;
    MerkleNode tree = MerkleEngine::build_tree(t.root, store, errors);

    std::vector<std::string> leaves;
    MerkleEngine::collect_leaves(tree, leaves);

    std::unordered_set<std::string> leaf_set(leaves.begin(), leaves.end());
    EXPECT_TRUE(leaf_set.count("a.txt"));
    EXPECT_TRUE(leaf_set.count("sub/b.txt"));
    EXPECT_TRUE(leaf_set.count("sub/deep/c.txt"));
    EXPECT_FALSE(leaf_set.count(".hidden")) << "Hidden files must not appear in leaves";
}

// ---------------------------------------------------------------------------
// Tombstoned entry preserves the original file hash
// ---------------------------------------------------------------------------

TEST(TombstoneScanTest, TombstonedEntryPreservesHash) {
    TempTree t("hash_preserved");
    t.write("data.txt", "important content");

    auto store = t.open_store("hash_preserved");
    scan_with_tombstone(t.root, store, "driveA");

    auto before = store.get_file("data.txt");
    ASSERT_TRUE(before.has_value());
    std::string original_hash = before->hash;
    EXPECT_FALSE(original_hash.empty());

    t.remove("data.txt");
    scan_with_tombstone(t.root, store, "driveA");

    auto after = store.get_file("data.txt");
    ASSERT_TRUE(after.has_value());
    EXPECT_TRUE(after->tombstone);
    EXPECT_EQ(after->hash, original_hash)
        << "Tombstone must preserve the last known hash for conflict resolution";
}

// ---------------------------------------------------------------------------
// Re-adding a previously tombstoned file clears the tombstone on next scan
// ---------------------------------------------------------------------------

TEST(TombstoneScanTest, RecreatedFileClearsTombstone) {
    TempTree t("recreate");
    t.write("file.txt", "original");

    auto store = t.open_store("recreate");
    scan_with_tombstone(t.root, store, "driveA");

    t.remove("file.txt");
    scan_with_tombstone(t.root, store, "driveA");
    EXPECT_TRUE(store.get_file("file.txt")->tombstone);

    // Re-create the file and scan again
    t.write("file.txt", "new content");
    scan_with_tombstone(t.root, store, "driveA");

    auto result = store.get_file("file.txt");
    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->tombstone) << "Re-created file must have tombstone cleared";
    EXPECT_FALSE(result->hash.empty());
}
