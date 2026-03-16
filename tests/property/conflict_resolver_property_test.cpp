#include "conflict_resolver.hpp"
#include "manifest_store.hpp"

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
           ("caravault_cr_prop_" + std::to_string(counter.fetch_add(1)) + ".db");
}

struct TempStore {
    fs::path path;
    ManifestStore store;

    explicit TempStore() : path(make_temp_db_path()), store(ManifestStore::open(path)) {}

    ~TempStore() {
        store.close();
        fs::remove(path);
        fs::remove(fs::path(path.string() + "-wal"));
        fs::remove(fs::path(path.string() + "-shm"));
    }
};

FileMetadata make_meta(const std::string& path,
                       const std::string& hash,
                       uint64_t mtime,
                       const VersionVector& vv,
                       bool tombstone = false) {
    FileMetadata m;
    m.path = path.empty() ? "file" : path;
    m.hash = hash.empty() ? "h" : hash;
    m.size = 0;
    m.mtime = mtime;
    m.version_vector = vv;
    m.tombstone = tombstone;
    return m;
}

ConflictInfo make_conflict(const std::string& path,
                           const std::map<std::string, FileMetadata>& versions) {
    return {path, versions, ResolutionStrategy::MANUAL_RESOLUTION};
}

rc::Gen<std::string> gen_nonempty_string() {
    return rc::gen::map(rc::gen::string<std::string>(),
                        [](std::string s) { return s.empty() ? std::string("x") : s; });
}

}  // namespace

// For any two file versions with CONCURRENT version vectors, the system SHALL
// identify the file as having a conflict.
RC_GTEST_PROP(ConflictResolverProperty, ConcurrentModificationConflictDetection, ()) {
    auto path = *gen_nonempty_string();
    auto hash_a = *gen_nonempty_string();
    auto hash_b = *gen_nonempty_string();

    VersionVector vv_a, vv_b;
    vv_a.increment("drive_a");
    vv_b.increment("drive_b");
    RC_PRE(vv_a.compare(vv_b) == VersionVector::Ordering::CONCURRENT);

    TempStore ts_a, ts_b;
    ts_a.store.upsert_file(make_meta(path, hash_a, 100, vv_a));
    ts_b.store.upsert_file(make_meta(path, hash_b, 200, vv_b));

    std::map<std::string, ManifestStore*> manifests{{"drive_a", &ts_a.store},
                                                    {"drive_b", &ts_b.store}};
    auto conflicts = ConflictResolver{}.detect_conflicts(manifests);

    RC_ASSERT(std::any_of(conflicts.begin(), conflicts.end(), [&](const ConflictInfo& ci) {
        return ci.path == path;
    }));
}

// For any detected conflict, the conflict record SHALL contain all conflicting
// versions with their drive identifiers.
RC_GTEST_PROP(ConflictResolverProperty, ConflictRecordCompleteness, ()) {
    auto path = *gen_nonempty_string();
    auto num_drives = *rc::gen::inRange<size_t>(2, 5);

    std::vector<TempStore> stores(num_drives);
    std::map<std::string, ManifestStore*> manifests;

    for (size_t i = 0; i < num_drives; ++i) {
        std::string drive_id = "drive_" + std::to_string(i);
        VersionVector vv;
        vv.increment(drive_id);
        stores[i].store.upsert_file(make_meta(path, "hash_" + std::to_string(i), 100 + i, vv));
        manifests[drive_id] = &stores[i].store;
    }

    auto conflicts = ConflictResolver{}.detect_conflicts(manifests);
    auto it = std::find_if(conflicts.begin(), conflicts.end(), [&](const ConflictInfo& ci) {
        return ci.path == path;
    });
    RC_ASSERT(it != conflicts.end());
    RC_ASSERT(it->versions.size() == num_drives);
    for (size_t i = 0; i < num_drives; ++i)
        RC_ASSERT(it->versions.count("drive_" + std::to_string(i)) == 1u);
}

// For any two file versions where one version vector DOMINATES the other,
// the system SHALL NOT mark the file as having a conflict.
RC_GTEST_PROP(ConflictResolverProperty, CausallyOrderedUpdatesNotConflicts, ()) {
    auto path = *gen_nonempty_string();

    VersionVector vv_old, vv_new;
    vv_old.increment("drive_a");
    vv_new = vv_old;
    vv_new.increment("drive_a");
    RC_PRE(vv_new.compare(vv_old) == VersionVector::Ordering::DOMINATES);

    TempStore ts_a, ts_b;
    ts_a.store.upsert_file(make_meta(path, "old", 100, vv_old));
    ts_b.store.upsert_file(make_meta(path, "new", 200, vv_new));

    std::map<std::string, ManifestStore*> manifests{{"drive_a", &ts_a.store},
                                                    {"drive_b", &ts_b.store}};
    auto conflicts = ConflictResolver{}.detect_conflicts(manifests);

    RC_ASSERT(!std::any_of(conflicts.begin(), conflicts.end(), [&](const ConflictInfo& ci) {
        return ci.path == path;
    }));
}

// For any conflict where >50% of all registered drives agree on a file version,
// the system SHALL select that version as authoritative.
RC_GTEST_PROP(ConflictResolverProperty, QuorumResolutionSelection, ()) {
    auto path = *gen_nonempty_string();

    // 5 total drives; 3 agree on quorum_hash -> quorum (3/5 > 50%)
    std::map<std::string, FileMetadata> versions;
    for (int i = 0; i < 3; ++i) {
        VersionVector vv;
        vv.increment("drive_q" + std::to_string(i));
        versions["drive_q" + std::to_string(i)] = make_meta(path, "quorum_hash", 100, vv);
    }
    for (int i = 0; i < 2; ++i) {
        VersionVector vv;
        vv.increment("drive_m" + std::to_string(i));
        versions["drive_m" + std::to_string(i)] = make_meta(path, "minority_hash", 200, vv);
    }

    auto res = ConflictResolver{}.resolve(make_conflict(path, versions), 5);

    RC_ASSERT(res.strategy_used == ResolutionStrategy::MAJORITY_QUORUM);
    RC_ASSERT(versions.at(res.winning_drive_id).hash == "quorum_hash");
}

// For any set of N registered drives, a quorum SHALL be defined as more than N/2 drives.
RC_GTEST_PROP(ConflictResolverProperty, QuorumThresholdDefinition, ()) {
    auto total_drives = *rc::gen::inRange<size_t>(2, 21);
    auto path = *gen_nonempty_string();

    size_t quorum_count = total_drives / 2 + 1;
    size_t non_quorum_count = total_drives / 2;

    // quorum_count drives agree -> MAJORITY_QUORUM
    {
        std::map<std::string, FileMetadata> versions;
        for (size_t i = 0; i < quorum_count; ++i) {
            VersionVector vv;
            vv.increment("dq" + std::to_string(i));
            versions["dq" + std::to_string(i)] = make_meta(path, "qhash", 100, vv);
        }
        for (size_t i = quorum_count; i < total_drives; ++i) {
            VersionVector vv;
            vv.increment("do" + std::to_string(i));
            versions["do" + std::to_string(i)] = make_meta(path, "ohash", 200, vv);
        }
        RC_ASSERT(
            ConflictResolver{}.resolve(make_conflict(path, versions), total_drives).strategy_used ==
            ResolutionStrategy::MAJORITY_QUORUM);
    }

    // non_quorum_count drives agree -> NOT MAJORITY_QUORUM
    if (non_quorum_count > 0 && non_quorum_count < total_drives) {
        std::map<std::string, FileMetadata> versions;
        for (size_t i = 0; i < non_quorum_count; ++i) {
            VersionVector vv;
            vv.increment("dq" + std::to_string(i));
            versions["dq" + std::to_string(i)] = make_meta(path, "qhash", 100, vv);
        }
        for (size_t i = non_quorum_count; i < total_drives; ++i) {
            VersionVector vv;
            vv.increment("do" + std::to_string(i));
            versions["do" + std::to_string(i)] =
                make_meta(path, "unique_" + std::to_string(i), 200, vv);
        }
        RC_ASSERT(
            ConflictResolver{}.resolve(make_conflict(path, versions), total_drives).strategy_used !=
            ResolutionStrategy::MAJORITY_QUORUM);
    }
}

// For any conflict where no version has a quorum, the system SHALL preserve all
// versions without selecting a quorum winner.
RC_GTEST_PROP(ConflictResolverProperty, NoQuorumPreservation, ()) {
    auto path = *gen_nonempty_string();

    // 4 drives, each with a unique hash -> no quorum possible
    std::map<std::string, FileMetadata> versions;
    for (size_t i = 0; i < 4; ++i) {
        VersionVector vv;
        vv.increment("drive_" + std::to_string(i));
        versions["drive_" + std::to_string(i)] =
            make_meta(path, "unique_" + std::to_string(i), 100 + i, vv);
    }

    auto res = ConflictResolver{}.resolve(make_conflict(path, versions), 4);

    RC_ASSERT(res.strategy_used != ResolutionStrategy::MAJORITY_QUORUM);
}

// For any conflict resolved by quorum, the resolution SHALL record that version
// vectors were merged to reflect the resolution.
RC_GTEST_PROP(ConflictResolverProperty, QuorumResolutionVersionVectorUpdate, ()) {
    auto path = *gen_nonempty_string();

    // 3 total drives; 2 agree -> quorum (2/3 > 50%)
    VersionVector vv_q0, vv_q1, vv_m;
    vv_q0.increment("drive_q0");
    vv_q1.increment("drive_q1");
    vv_m.increment("drive_m");

    std::map<std::string, FileMetadata> versions{
        {"drive_q0", make_meta(path, "qhash", 100, vv_q0)},
        {"drive_q1", make_meta(path, "qhash", 100, vv_q1)},
        {"drive_m", make_meta(path, "other", 200, vv_m)},
    };

    auto res = ConflictResolver{}.resolve(make_conflict(path, versions), 3);

    RC_ASSERT(res.strategy_used == ResolutionStrategy::MAJORITY_QUORUM);
    RC_ASSERT(versions.at(res.winning_drive_id).hash == "qhash");
    RC_ASSERT(std::any_of(res.actions.begin(), res.actions.end(), [](const std::string& a) {
        return a.find("merged") != std::string::npos || a.find("Version") != std::string::npos;
    }));
}
