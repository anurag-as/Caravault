#include "conflict_resolver.hpp"
#include "executor.hpp"
#include "manifest_store.hpp"
#include "merkle_engine.hpp"
#include "sync_planner.hpp"
#include "version_vector.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <set>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

std::atomic<int> g_counter{0};

fs::path make_temp_dir() {
    fs::path p =
        fs::temp_directory_path() / ("caravault_idem_" + std::to_string(g_counter.fetch_add(1)));
    fs::create_directories(p);
    return p;
}

// RAII wrapper for a single simulated drive: a root directory + manifest DB.
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

// Run one full sync round: detect conflicts, resolve, plan, execute.
void run_sync(const std::map<std::string, fs::path>& roots,
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

// Count non-MKDIR operations returned by the planner (used to verify
// idempotence: a second sync with no changes must return zero such ops).
size_t count_file_ops(std::map<std::string, ManifestStore*>& manifests) {
    auto conflicts = ConflictResolver{}.detect_conflicts(manifests);
    std::vector<Resolution> resolutions;
    for (const auto& c : conflicts)
        resolutions.push_back(ConflictResolver{}.resolve(c, manifests.size()));

    auto ops = SyncPlanner{}.plan_sync(manifests, resolutions);
    return std::count_if(
        ops.begin(), ops.end(), [](const SyncOp& op) { return op.type != SyncOpType::MKDIR; });
}

// Verify that every drive's manifest agrees on the hash for every live path.
bool drives_are_consistent(const std::map<std::string, ManifestStore*>& manifests) {
    // Build canonical hash map from the union of all live paths.
    std::map<std::string, std::string> canonical;  // path -> expected hash
    for (const auto& [drive_id, store] : manifests) {
        for (const auto& meta : store->get_all_files()) {
            if (meta.tombstone)
                continue;
            auto it = canonical.find(meta.path);
            if (it == canonical.end()) {
                canonical[meta.path] = meta.hash;
            } else if (it->second != meta.hash) {
                return false;
            }
        }
    }

    // Every drive must carry every live path with the canonical hash.
    for (const auto& [path, expected_hash] : canonical) {
        for (const auto& [drive_id, store] : manifests) {
            auto meta = store->get_file(path);
            if (!meta.has_value() || meta->tombstone || meta->hash != expected_hash)
                return false;
        }
    }
    return true;
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

enum class ModOp { ADD, MODIFY, DELETE };

struct FileMod {
    ModOp op;
    std::string filename;
    std::string content;
};

rc::Gen<FileMod> gen_file_mod(const std::vector<std::string>& existing_files) {
    if (existing_files.empty()) {
        return rc::gen::map(rc::gen::pair(gen_filename(), gen_content()),
                            [](auto p) -> FileMod { return {ModOp::ADD, p.first, p.second}; });
    }
    return rc::gen::map(rc::gen::tuple(rc::gen::inRange<int>(0, 3),
                                       gen_filename(),
                                       gen_content(),
                                       rc::gen::inRange<size_t>(0, existing_files.size())),
                        [existing_files](auto t) -> FileMod {
                            int op_idx = std::get<0>(t);
                            const std::string& existing = existing_files[std::get<3>(t)];
                            if (op_idx == 0)
                                return {ModOp::ADD, std::get<1>(t), std::get<2>(t)};
                            if (op_idx == 1)
                                return {ModOp::MODIFY, existing, std::get<2>(t)};
                            return {ModOp::DELETE, existing, ""};
                        });
}

}  // namespace

// FOR ALL valid file operations, performing sync then modifying then syncing
// again SHALL result in consistent state across all Drives, and syncing again
// without modifications SHALL produce no file transfer operations.
RC_GTEST_PROP(IdempotenceProperty, SynchronizationIdempotence, ()) {
    // Generate 2-4 drives and 1-6 initial files placed on drive_0.
    auto num_drives = *rc::gen::inRange<size_t>(2, 5);
    auto num_files = *rc::gen::inRange<size_t>(1, 7);

    auto filenames = *rc::gen::container<std::vector<std::string>>(num_files, gen_filename());
    auto contents = *rc::gen::container<std::vector<std::string>>(num_files, gen_content());

    // Deduplicate filenames to avoid aliasing in the initial state.
    {
        std::set<std::string> seen;
        for (auto& fn : filenames) {
            while (seen.count(fn))
                fn += "x";
            seen.insert(fn);
        }
    }

    fs::path base = make_temp_dir();

    // Create drives.
    std::vector<std::unique_ptr<TempDrive>> drives;
    drives.reserve(num_drives);
    for (size_t i = 0; i < num_drives; ++i)
        drives.push_back(std::make_unique<TempDrive>(base, "drive_" + std::to_string(i)));

    std::map<std::string, fs::path> roots;
    std::map<std::string, ManifestStore*> manifests;
    for (auto& d : drives) {
        roots[d->drive_id] = d->root;
        manifests[d->drive_id] = &d->store;
    }

    // Populate drive_0 with the initial files and scan into manifest.
    TempDrive& d0 = *drives[0];
    for (size_t i = 0; i < num_files; ++i)
        write_file(d0.root / filenames[i], contents[i]);
    MerkleEngine::build_tree(d0.root, d0.store);

    // First sync – all drives should reach consistent state.
    run_sync(roots, manifests);

    RC_ASSERT(drives_are_consistent(manifests));

    // Every initial file must be physically present on every drive.
    for (const auto& fn : filenames)
        for (auto& d : drives)
            RC_ASSERT(fs::exists(d->root / fn));

    // Apply random modifications to a randomly chosen drive.
    auto mod_drive_idx = *rc::gen::inRange<size_t>(0, num_drives);
    TempDrive& mod_drive = *drives[mod_drive_idx];

    // Collect the current live files on the chosen drive.
    std::vector<std::string> live_files;
    for (const auto& meta : mod_drive.store.get_all_files())
        if (!meta.tombstone)
            live_files.push_back(meta.path);

    auto num_mods = *rc::gen::inRange<size_t>(0, 4);
    for (size_t m = 0; m < num_mods; ++m) {
        auto mod = *gen_file_mod(live_files);

        if (mod.op == ModOp::ADD) {
            // Avoid colliding with an existing live file.
            std::string fn = mod.filename;
            while (std::find(live_files.begin(), live_files.end(), fn) != live_files.end())
                fn += "a";
            write_file(mod_drive.root / fn, mod.content);
            live_files.push_back(fn);
        } else if (mod.op == ModOp::MODIFY) {
            write_file(mod_drive.root / mod.filename, mod.content);
        } else {
            // DELETE
            fs::remove(mod_drive.root / mod.filename);
            live_files.erase(std::remove(live_files.begin(), live_files.end(), mod.filename),
                             live_files.end());
        }
    }

    // Re-scan the modified drive so its manifest reflects the changes.
    MerkleEngine::build_tree(mod_drive.root, mod_drive.store);

    // Second sync – drives must again reach consistent state.
    run_sync(roots, manifests);

    RC_ASSERT(drives_are_consistent(manifests));

    // Idempotence check – a third sync with no changes must produce
    //          zero file-transfer operations.
    RC_ASSERT(count_file_ops(manifests) == 0u);

    fs::remove_all(base);
}

// FOR ALL pairs of drives that are already in sync, running sync SHALL produce
// no file transfer operations (pure idempotence on an already-consistent state).
RC_GTEST_PROP(IdempotenceProperty, AlreadyConsistentStateProducesNoOps, ()) {
    auto num_drives = *rc::gen::inRange<size_t>(2, 5);
    auto num_files = *rc::gen::inRange<size_t>(1, 6);

    auto filenames = *rc::gen::container<std::vector<std::string>>(num_files, gen_filename());
    auto contents = *rc::gen::container<std::vector<std::string>>(num_files, gen_content());

    // Deduplicate filenames.
    {
        std::set<std::string> seen;
        for (auto& fn : filenames) {
            while (seen.count(fn))
                fn += "x";
            seen.insert(fn);
        }
    }

    fs::path base = make_temp_dir();

    std::vector<std::unique_ptr<TempDrive>> drives;
    drives.reserve(num_drives);
    for (size_t i = 0; i < num_drives; ++i)
        drives.push_back(std::make_unique<TempDrive>(base, "drive_" + std::to_string(i)));

    std::map<std::string, fs::path> roots;
    std::map<std::string, ManifestStore*> manifests;
    for (auto& d : drives) {
        roots[d->drive_id] = d->root;
        manifests[d->drive_id] = &d->store;
    }

    // Write identical files to every drive and build manifests with a shared
    // version vector so the planner sees them as already in sync.
    VersionVector shared_vv;
    shared_vv.increment("drive_0");

    for (size_t i = 0; i < num_files; ++i)
        for (auto& d : drives)
            write_file(d->root / filenames[i], contents[i]);

    for (auto& d : drives) {
        MerkleEngine::build_tree(d->root, d->store);
        // Overwrite version vectors so all drives agree causally.
        for (size_t i = 0; i < num_files; ++i) {
            auto meta = d->store.get_file(filenames[i]);
            if (meta.has_value()) {
                meta->version_vector = shared_vv;
                d->store.upsert_file(*meta);
            }
        }
    }

    // A sync on an already-consistent state must produce no file-transfer ops.
    RC_ASSERT(count_file_ops(manifests) == 0u);

    fs::remove_all(base);
}
