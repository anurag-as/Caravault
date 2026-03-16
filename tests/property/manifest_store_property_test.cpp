#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include "manifest_store.hpp"
#include "version_vector.hpp"

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;
using namespace caravault;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Generate a unique temp DB path for each test invocation.
fs::path make_temp_db_path() {
    static std::atomic<int> counter{0};
    auto tmp = fs::temp_directory_path() /
               ("caravault_prop_" + std::to_string(counter.fetch_add(1)) + ".db");
    return tmp;
}

// RAII wrapper: opens a fresh ManifestStore and removes the file on destruction.
struct TempStore {
    fs::path path;
    ManifestStore store;

    explicit TempStore()
        : path(make_temp_db_path()),
          store(ManifestStore::open(path)) {}

    ~TempStore() {
        store.close();
        fs::remove(path);
        // Also remove WAL/SHM sidecar files
        fs::remove(fs::path(path.string() + "-wal"));
        fs::remove(fs::path(path.string() + "-shm"));
    }
};

// Build a non-empty string from a vector of chars (avoids empty drive IDs).
std::string nonempty_string(const std::string& s) {
    return s.empty() ? "x" : s;
}

// Build a FileMetadata from generated components.
FileMetadata make_metadata(const std::string& path,
                           const std::string& hash,
                           uint64_t size,
                           uint64_t mtime,
                           bool tombstone,
                           bool has_inode,
                           uint64_t inode_val) {
    FileMetadata m;
    m.path      = path.empty() ? "file" : path;
    m.hash      = hash.empty() ? "0" : hash;
    m.size      = size;
    m.mtime     = mtime;
    m.tombstone = tombstone;
    m.inode     = has_inode ? std::optional<uint64_t>{inode_val} : std::nullopt;
    return m;
}

} // anonymous namespace

// For any sequence of drive detection events, all assigned drive identifiers
// SHALL be unique across all drives.
RC_GTEST_PROP(ManifestStoreProperty, Property1_DriveIdUniqueness, ()) {
    // Generate a list of drive IDs (may contain duplicates in input)
    auto raw_ids = *rc::gen::container<std::vector<std::string>>(
        rc::gen::map(rc::gen::string<std::string>(), [](std::string s) {
            return s.empty() ? std::string("d") : s;
        })
    );

    TempStore ts;
    for (const auto& id : raw_ids) {
        ts.store.register_drive(id);
    }

    auto drives = ts.store.get_all_drives();

    // All returned drive IDs must be unique
    std::set<std::string> unique_drives(drives.begin(), drives.end());
    RC_ASSERT(unique_drives.size() == drives.size());
}

// For any drive, after registering the drive, disconnecting it (closing the
// store), and reconnecting it (reopening the store), the system SHALL
// recognize the same drive identifier.
RC_GTEST_PROP(ManifestStoreProperty, Property2_DriveRecognitionRoundTrip, ()) {
    auto drive_id = nonempty_string(*rc::gen::string<std::string>());

    fs::path db_path = make_temp_db_path();

    // Register the drive
    {
        auto store = ManifestStore::open(db_path);
        store.register_drive(drive_id);
        store.close();
    }

    // Reopen (simulate reconnect) and verify recognition
    {
        auto store = ManifestStore::open(db_path);
        auto drives = store.get_all_drives();
        bool found = std::find(drives.begin(), drives.end(), drive_id) != drives.end();
        RC_ASSERT(found);
        store.close();
    }

    fs::remove(db_path);
    fs::remove(fs::path(db_path.string() + "-wal"));
    fs::remove(fs::path(db_path.string() + "-shm"));
}

// For any set of registered drives, querying the drive registry SHALL return
// all registered drives with their identifiers.
RC_GTEST_PROP(ManifestStoreProperty, Property3_DriveRegistryCompleteness, ()) {
    // Generate a set of unique non-empty drive IDs
    auto raw_ids = *rc::gen::container<std::vector<std::string>>(
        rc::gen::map(rc::gen::string<std::string>(), [](std::string s) {
            return s.empty() ? std::string("d") : s;
        })
    );

    // Deduplicate to form the expected set
    std::set<std::string> expected(raw_ids.begin(), raw_ids.end());

    TempStore ts;
    for (const auto& id : expected) {
        ts.store.register_drive(id);
    }

    auto drives = ts.store.get_all_drives();
    std::set<std::string> actual(drives.begin(), drives.end());

    // Every registered drive must appear in the registry
    RC_ASSERT(actual == expected);
}

// For any file metadata, storing it to the SQLite database and reloading it
// SHALL produce equal metadata.
RC_GTEST_PROP(ManifestStoreProperty, Property41_FileMetadataPersistenceRoundTrip, ()) {
    auto path_str   = *rc::gen::map(rc::gen::string<std::string>(),
                                    [](std::string s){ return s.empty() ? std::string("f") : s; });
    auto hash_str   = *rc::gen::map(rc::gen::string<std::string>(),
                                    [](std::string s){ return s.empty() ? std::string("0") : s; });
    auto size_val   = *rc::gen::inRange<uint64_t>(0, UINT64_MAX);
    auto mtime_val  = *rc::gen::inRange<uint64_t>(0, UINT64_MAX);
    auto tombstone  = *rc::gen::arbitrary<bool>();
    auto has_inode  = *rc::gen::arbitrary<bool>();
    auto inode_val  = *rc::gen::inRange<uint64_t>(0, UINT64_MAX);

    // Build a VersionVector with a few arbitrary clocks
    auto clock_pairs = *rc::gen::container<std::vector<std::pair<std::string, uint64_t>>>(
        rc::gen::pair(
            rc::gen::map(rc::gen::string<std::string>(),
                         [](std::string s){ return s.empty() ? std::string("d") : s; }),
            rc::gen::inRange<uint64_t>(0, 1000)
        )
    );
    std::map<std::string, uint64_t> clocks;
    for (const auto& [k, v] : clock_pairs) {
        clocks[k] = v;
    }

    FileMetadata original = make_metadata(path_str, hash_str, size_val, mtime_val,
                                          tombstone, has_inode, inode_val);
    original.version_vector = VersionVector(clocks);

    TempStore ts;
    ts.store.upsert_file(original);

    auto result = ts.store.get_file(original.path);
    RC_ASSERT(result.has_value());
    RC_ASSERT(*result == original);
}

// For any Merkle tree structure, storing it to the SQLite database and
// reloading it SHALL produce an equal tree structure.
RC_GTEST_PROP(ManifestStoreProperty, Property42_MerkleTreePersistenceRoundTrip, ()) {
    auto path_str = *rc::gen::map(rc::gen::string<std::string>(),
                                  [](std::string s){ return s.empty() ? std::string("p") : s; });
    auto hash_str = *rc::gen::map(rc::gen::string<std::string>(),
                                  [](std::string s){ return s.empty() ? std::string("h") : s; });
    auto level    = *rc::gen::inRange<int>(0, 100);

    TempStore ts;
    ts.store.upsert_merkle_node(path_str, hash_str, level);

    auto result = ts.store.get_merkle_hash(path_str, level);
    RC_ASSERT(result.has_value());
    RC_ASSERT(*result == hash_str);
}

// For any drive registry information, storing it to the SQLite database and
// reloading it SHALL produce equal registry data.
RC_GTEST_PROP(ManifestStoreProperty, Property43_DriveRegistryPersistenceRoundTrip, ()) {
    auto raw_ids = *rc::gen::container<std::vector<std::string>>(
        rc::gen::map(rc::gen::string<std::string>(),
                     [](std::string s){ return s.empty() ? std::string("d") : s; })
    );
    std::set<std::string> expected(raw_ids.begin(), raw_ids.end());

    fs::path db_path = make_temp_db_path();

    // Write registry
    {
        auto store = ManifestStore::open(db_path);
        for (const auto& id : expected) {
            store.register_drive(id);
        }
        store.close();
    }

    // Reload and verify
    {
        auto store = ManifestStore::open(db_path);
        auto drives = store.get_all_drives();
        std::set<std::string> actual(drives.begin(), drives.end());
        RC_ASSERT(actual == expected);
        store.close();
    }

    fs::remove(db_path);
    fs::remove(fs::path(db_path.string() + "-wal"));
    fs::remove(fs::path(db_path.string() + "-shm"));
}

// The system SHALL store database files with restricted file permissions
// (not world-writable).
RC_GTEST_PROP(ManifestStoreProperty, Property68_DatabaseFilePermissions, ()) {
    fs::path db_path = make_temp_db_path();
    {
        auto store = ManifestStore::open(db_path);
        store.close();
    }

#ifndef _WIN32
    struct stat st{};
    RC_ASSERT(::stat(db_path.string().c_str(), &st) == 0);
    RC_ASSERT((st.st_mode & S_IWOTH) == 0);
#endif

    fs::remove(db_path);
    fs::remove(fs::path(db_path.string() + "-wal"));
    fs::remove(fs::path(db_path.string() + "-shm"));
}
