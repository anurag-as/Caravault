#include "manifest_store.hpp"
#include "merkle_engine.hpp"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

// Creates a temp directory tree for each test and removes it on destruction.
struct TempTree {
    fs::path root;

    explicit TempTree(const std::string& name) {
        root = fs::temp_directory_path() / ("caravault_skip_test_" + name);
        fs::remove_all(root);
        fs::create_directories(root);
    }

    ~TempTree() { fs::remove_all(root); }

    // Write content to a relative path under root, creating parent dirs.
    fs::path write(const std::string& rel, const std::string& content = "data") {
        fs::path p = root / rel;
        fs::create_directories(p.parent_path());
        std::ofstream f(p);
        f << content;
        return p;
    }

    // Open (or create) a ManifestStore in a temp location outside the tree.
    ManifestStore open_store(const std::string& tag) {
        fs::path db = fs::temp_directory_path() / ("caravault_skip_manifest_" + tag + ".db");
        fs::remove(db);
        return ManifestStore::open(db);
    }
};

// Collect all relative paths recorded in the manifest after a scan.
std::vector<std::string> scanned_paths(ManifestStore& store) {
    std::vector<std::string> paths;
    for (const auto& f : store.get_all_files())
        paths.push_back(f.path);
    return paths;
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

// ---------------------------------------------------------------------------
// Hidden files (dot-prefixed names) are excluded from the scan
// ---------------------------------------------------------------------------

TEST(MerkleEngineSkipTest, HiddenFileIsNotScanned) {
    TempTree t("hidden_file");
    t.write("visible.txt", "hello");
    t.write(".hidden_file", "secret");

    auto store = t.open_store("hidden_file");
    std::vector<ScanError> errors;
    MerkleEngine::build_tree(t.root, store, errors);

    auto paths = scanned_paths(store);
    EXPECT_TRUE(contains(paths, "visible.txt"));
    EXPECT_FALSE(contains(paths, ".hidden_file"))
        << ".hidden_file should be excluded from the manifest";
}

TEST(MerkleEngineSkipTest, HiddenDirectoryIsNotScanned) {
    TempTree t("hidden_dir");
    t.write("visible.txt", "hello");
    t.write(".hidden_dir/inside.txt", "secret");

    auto store = t.open_store("hidden_dir");
    std::vector<ScanError> errors;
    MerkleEngine::build_tree(t.root, store, errors);

    auto paths = scanned_paths(store);
    EXPECT_TRUE(contains(paths, "visible.txt"));
    EXPECT_FALSE(contains(paths, ".hidden_dir/inside.txt"))
        << "Files inside a hidden directory should be excluded";
}

// ---------------------------------------------------------------------------
// .caravault directory (manifest DB location) is excluded
// ---------------------------------------------------------------------------

TEST(MerkleEngineSkipTest, CaravaultDirIsNotScanned) {
    TempTree t("caravault_dir");
    t.write("readme.txt", "hello");
    t.write(".caravault/manifest.db", "sqlite_data");
    t.write(".caravault/manifest.db-wal", "wal_data");
    t.write(".caravault/manifest.db-shm", "shm_data");

    auto store = t.open_store("caravault_dir");
    std::vector<ScanError> errors;
    MerkleEngine::build_tree(t.root, store, errors);

    auto paths = scanned_paths(store);
    EXPECT_TRUE(contains(paths, "readme.txt"));
    EXPECT_FALSE(contains(paths, ".caravault/manifest.db"))
        << "manifest.db must not be tracked in the manifest";
    EXPECT_FALSE(contains(paths, ".caravault/manifest.db-wal"));
    EXPECT_FALSE(contains(paths, ".caravault/manifest.db-shm"));
}

// ---------------------------------------------------------------------------
// macOS resource fork files (._filename) are excluded
// ---------------------------------------------------------------------------

TEST(MerkleEngineSkipTest, MacOSResourceForkIsNotScanned) {
    TempTree t("resource_fork");
    t.write("photo.jpg", "jpeg_data");
    t.write("._photo.jpg", "resource_fork_data");

    auto store = t.open_store("resource_fork");
    std::vector<ScanError> errors;
    MerkleEngine::build_tree(t.root, store, errors);

    auto paths = scanned_paths(store);
    EXPECT_TRUE(contains(paths, "photo.jpg"));
    EXPECT_FALSE(contains(paths, "._photo.jpg"))
        << "macOS resource fork files (._*) should be excluded";
}

// ---------------------------------------------------------------------------
// Visible files at various depths are still scanned normally
// ---------------------------------------------------------------------------

TEST(MerkleEngineSkipTest, VisibleFilesAtAllDepthsAreScanned) {
    TempTree t("visible_files");
    t.write("top.txt");
    t.write("subdir/mid.txt");
    t.write("subdir/nested/deep.txt");
    // Mix in hidden entries that must not appear
    t.write(".hidden_top");
    t.write("subdir/.hidden_mid");
    t.write(".hidden_dir/visible_inside.txt");

    auto store = t.open_store("visible_files");
    std::vector<ScanError> errors;
    MerkleEngine::build_tree(t.root, store, errors);

    auto paths = scanned_paths(store);
    EXPECT_TRUE(contains(paths, "top.txt"));
    EXPECT_TRUE(contains(paths, "subdir/mid.txt"));
    EXPECT_TRUE(contains(paths, "subdir/nested/deep.txt"));

    EXPECT_FALSE(contains(paths, ".hidden_top"));
    EXPECT_FALSE(contains(paths, "subdir/.hidden_mid"));
    EXPECT_FALSE(contains(paths, ".hidden_dir/visible_inside.txt"));
}

// ---------------------------------------------------------------------------
// Root hash is stable: re-scanning an unchanged tree yields the same hash
// ---------------------------------------------------------------------------

TEST(MerkleEngineSkipTest, RootHashStableWithHiddenFilesPresent) {
    TempTree t("hash_stable");
    t.write("a.txt", "content_a");
    t.write("b.txt", "content_b");
    t.write(".hidden", "changes_every_time");
    t.write(".caravault/manifest.db", "live_db");

    auto store1 = t.open_store("hash_stable_1");
    std::vector<ScanError> e1;
    auto tree1 = MerkleEngine::build_tree(t.root, store1, e1);

    auto store2 = t.open_store("hash_stable_2");
    std::vector<ScanError> e2;
    auto tree2 = MerkleEngine::build_tree(t.root, store2, e2);

    EXPECT_EQ(tree1.hash, tree2.hash)
        << "Root hash must be identical across two scans of the same visible content";
}

// ---------------------------------------------------------------------------
// Hidden files do not affect the root hash
// ---------------------------------------------------------------------------

TEST(MerkleEngineSkipTest, RootHashUnaffectedByHiddenFileChanges) {
    TempTree t("hash_hidden");
    t.write("data.txt", "important");

    auto store1 = t.open_store("hash_hidden_before");
    std::vector<ScanError> e1;
    auto tree_before = MerkleEngine::build_tree(t.root, store1, e1);

    // Add / modify hidden files — root hash must not change.
    t.write(".new_hidden", "new secret");
    t.write(".caravault/manifest.db", "updated db bytes");

    auto store2 = t.open_store("hash_hidden_after");
    std::vector<ScanError> e2;
    auto tree_after = MerkleEngine::build_tree(t.root, store2, e2);

    EXPECT_EQ(tree_before.hash, tree_after.hash)
        << "Adding hidden files must not change the root hash of visible content";
}

// ---------------------------------------------------------------------------
// No scan errors are emitted for skipped entries
// ---------------------------------------------------------------------------

TEST(MerkleEngineSkipTest, NoScanErrorsForSkippedEntries) {
    TempTree t("no_errors");
    t.write("ok.txt", "fine");
    t.write(".hidden", "skip me");
    t.write(".caravault/manifest.db", "skip me too");

    auto store = t.open_store("no_errors");
    std::vector<ScanError> errors;
    MerkleEngine::build_tree(t.root, store, errors);

    // None of the errors should reference skipped paths.
    for (const auto& err : errors) {
        EXPECT_EQ(err.path.find(".hidden"), std::string::npos)
            << "Unexpected error for hidden path: " << err.path;
        EXPECT_EQ(err.path.find(".caravault"), std::string::npos)
            << "Unexpected error for .caravault path: " << err.path;
    }
}
