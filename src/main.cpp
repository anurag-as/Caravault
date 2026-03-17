#include "config.hpp"
#include "conflict_resolver.hpp"
#include "executor.hpp"
#include "manifest_store.hpp"
#include "merkle_engine.hpp"
#include "platform.hpp"
#include "progress_reporter.hpp"
#include "sync_planner.hpp"
#include "version_vector.hpp"

#include <CLI/CLI.hpp>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace caravault;

static fs::path config_path() {
    return fs::path(".caravault") / "caravault.conf";
}

static Config load_config() {
    Config cfg;
    fs::path p = config_path();
    if (fs::exists(p))
        cfg.load_from_file(p);
    return cfg;
}

// Returns the drive ID already stored in the manifest, or generates one from
// the mount-point hint, registers it, and returns it.
static std::string get_or_register_drive_id(ManifestStore& store, const std::string& hint) {
    auto drives = store.get_all_drives();
    if (!drives.empty())
        return drives.front();

    std::string id;
    for (char c : hint)
        if (std::isalnum(static_cast<unsigned char>(c)))
            id += c;
    if (id.empty())
        id = "drive";

    store.register_drive(id);
    return id;
}

static fs::path manifest_db_path(const fs::path& mount, const Config& cfg) {
    return mount / cfg.manifest_db_name;
}

static std::string sync_op_type_str(SyncOpType t) {
    switch (t) {
        case SyncOpType::COPY:
            return "COPY";
        case SyncOpType::REPLACE:
            return "REPLACE";
        case SyncOpType::DELETE:
            return "DELETE";
        case SyncOpType::RENAME:
            return "RENAME";
        case SyncOpType::MKDIR:
            return "MKDIR";
    }
    return "UNKNOWN";
}

static std::string resolution_strategy_str(ResolutionStrategy s) {
    switch (s) {
        case ResolutionStrategy::DOMINANT_VERSION:
            return "DOMINANT_VERSION";
        case ResolutionStrategy::MAJORITY_QUORUM:
            return "MAJORITY_QUORUM";
        case ResolutionStrategy::TOMBSTONE_WINS:
            return "TOMBSTONE_WINS";
        case ResolutionStrategy::LAST_WRITE_WINS:
            return "LAST_WRITE_WINS";
        case ResolutionStrategy::COPY_ALL_VERSIONS:
            return "COPY_ALL_VERSIONS";
        case ResolutionStrategy::MANUAL_RESOLUTION:
            return "MANUAL_RESOLUTION";
    }
    return "UNKNOWN";
}

// Resolves the set of drive mount points to operate on from --all / --drive flags.
// Returns an empty vector and prints an error (exit non-zero) when neither flag is given.
// On success, populates targets and returns true; on error returns false.
static bool resolve_drive_targets(bool all,
                                  const std::vector<std::string>& drive_paths,
                                  const std::string& command_name,
                                  std::vector<fs::path>& targets) {
    if (all) {
        auto detected = detect_drives();
        if (detected.empty()) {
            std::cerr << "No drives detected on this platform.\n";
            return false;
        }
        for (const auto& d : detected)
            targets.push_back(d.mount_point);
        return true;
    }

    if (!drive_paths.empty()) {
        for (const auto& p : drive_paths)
            targets.emplace_back(p);
        return true;
    }

    std::cerr << "Error: specify --all or --drive <path> for '" << command_name << "'.\n";
    std::cerr << "Currently detected drives:\n";
    auto detected = detect_drives();
    if (detected.empty()) {
        std::cerr << "  (none)\n";
    } else {
        for (const auto& d : detected)
            std::cerr << "  " << d.mount_point.string() << "  (" << d.drive_id_hint << ")\n";
    }
    return false;
}

// Opens manifest stores for the given mount-point paths.
// Populates manifests (drive_id -> store ptr), stores_owned (owns the stores),
// and drive_roots (drive_id -> mount point).
static void open_drive_manifests(
    const std::vector<fs::path>& mounts,
    const Config& cfg,
    std::map<std::string, std::unique_ptr<ManifestStore>>& stores_owned,
    std::map<std::string, ManifestStore*>& manifests,
    std::map<std::string, fs::path>& drive_roots) {
    for (const auto& mount : mounts) {
        fs::path db_path = manifest_db_path(mount, cfg);
        if (!fs::exists(db_path))
            continue;
        try {
            auto store = std::make_unique<ManifestStore>(ManifestStore::open(db_path));
            auto drives = store->get_all_drives();
            std::string id = drives.empty() ? mount.filename().string() : drives.front();
            drive_roots[id] = mount;
            manifests[id] = store.get();
            stores_owned[id] = std::move(store);
        } catch (const std::exception& ex) {
            std::cerr << "Warning: cannot open manifest on " << mount.string() << ": " << ex.what()
                      << "\n";
        }
    }
}

static int do_scan(const std::vector<std::string>& drive_paths, bool scan_all, const Config& cfg) {
    std::vector<fs::path> targets;
    if (!resolve_drive_targets(scan_all, drive_paths, "scan", targets))
        return 1;

    int exit_code = 0;
    for (const auto& mount : targets) {
        if (!fs::exists(mount)) {
            std::cerr << "Drive path does not exist: " << mount.string() << "\n";
            exit_code = 1;
            continue;
        }

        fs::path db_path = manifest_db_path(mount, cfg);
        fs::create_directories(db_path.parent_path());

        try {
            auto store = ManifestStore::open(db_path);
            std::string drive_id = get_or_register_drive_id(store, mount.filename().string());

            std::cout << "Scanning drive: " << drive_id << " at " << mount.string() << "\n";

            std::vector<ScanError> errors;
            ProgressReporter reporter;
            reporter.start(0, 0);  // totals unknown before scan
            MerkleNode tree = MerkleEngine::build_tree(mount, store, errors, &reporter);
            ProgressReporter::SummaryStats stats;
            reporter.finish(stats);

            for (const auto& err : errors)
                std::cerr << "  Warning: " << err.path << ": " << err.reason << "\n";

            std::cout << "  Root hash: " << tree.hash << "\n";
            std::cout << "  Scan complete.\n";
            store.close();
        } catch (const std::exception& ex) {
            std::cerr << "Error scanning " << mount.string() << ": " << ex.what() << "\n";
            exit_code = 1;
        }
    }
    return exit_code;
}

static int do_status(const std::vector<std::string>& drive_paths, bool all, const Config& cfg) {
    std::vector<fs::path> targets;
    if (!resolve_drive_targets(all, drive_paths, "status", targets))
        return 1;

    std::cout << "Connected drives:\n";
    bool any = false;
    for (const auto& mount : targets) {
        fs::path db_path = manifest_db_path(mount, cfg);
        std::string drive_id = mount.filename().string();
        size_t file_count = 0;
        uint64_t total_size = 0;

        if (fs::exists(db_path)) {
            try {
                auto store = ManifestStore::open(db_path);
                auto drives = store.get_all_drives();
                if (!drives.empty())
                    drive_id = drives.front();
                for (const auto& f : store.get_all_files()) {
                    if (!f.tombstone) {
                        ++file_count;
                        total_size += f.size;
                    }
                }
                store.close();
            } catch (const std::exception& ex) {
                std::cerr << "Warning: cannot read manifest on " << mount.string() << ": "
                          << ex.what() << "\n";
            }
        }

        std::cout << "  ID:         " << drive_id << "\n";
        std::cout << "  Mount:      " << mount.string() << "\n";
        std::cout << "  Files:      " << file_count << "\n";
        std::cout << "  Total size: " << total_size << " bytes\n\n";
        any = true;
    }
    if (!any)
        std::cout << "No drives found at the specified paths.\n";
    return 0;
}

static int do_conflicts(const std::vector<std::string>& drive_paths, bool all, const Config& cfg) {
    std::vector<fs::path> targets;
    if (!resolve_drive_targets(all, drive_paths, "conflicts", targets))
        return 1;

    std::map<std::string, std::unique_ptr<ManifestStore>> stores_owned;
    std::map<std::string, ManifestStore*> manifests;
    std::map<std::string, fs::path> drive_roots;
    open_drive_manifests(targets, cfg, stores_owned, manifests, drive_roots);

    if (manifests.empty()) {
        std::cout << "No manifest databases found. Run 'caravault scan' first.\n";
        return 0;
    }

    ConflictResolver resolver;
    auto conflicts = resolver.detect_conflicts(manifests);

    if (conflicts.empty()) {
        std::cout << "No conflicts detected.\n";
        return 0;
    }

    std::cout << conflicts.size() << " conflict(s) detected:\n\n";
    for (const auto& c : conflicts) {
        std::cout << "  Path: " << c.path << "\n";
        std::cout << "  Versions:\n";
        for (const auto& [drive_id, meta] : c.versions)
            std::cout << "    Drive " << drive_id << ": hash=" << meta.hash << " size=" << meta.size
                      << "\n";
        std::cout << "  Recommended strategy: " << resolution_strategy_str(c.recommended_strategy)
                  << "\n\n";
    }
    return 0;
}

static int do_resolve(const std::string& path,
                      const std::string& use_drive_id,
                      const std::vector<std::string>& drive_paths,
                      bool all,
                      const Config& cfg) {
    std::vector<fs::path> targets;
    if (!resolve_drive_targets(all, drive_paths, "resolve", targets))
        return 1;

    std::map<std::string, std::unique_ptr<ManifestStore>> stores_owned;
    std::map<std::string, ManifestStore*> manifests;
    std::map<std::string, fs::path> drive_roots;
    open_drive_manifests(targets, cfg, stores_owned, manifests, drive_roots);

    if (manifests.find(use_drive_id) == manifests.end()) {
        std::cerr << "Drive '" << use_drive_id << "' not found or has no manifest.\n";
        return 1;
    }

    auto* winning_store = manifests[use_drive_id];
    auto winning_meta = winning_store->get_file(path);
    if (!winning_meta) {
        std::cerr << "File '" << path << "' not found on drive '" << use_drive_id << "'.\n";
        return 1;
    }

    // Merge all known version vectors for this path, then advance the winner's clock.
    VersionVector merged_vv = winning_meta->version_vector;
    for (auto& [id, store] : manifests) {
        auto meta = store->get_file(path);
        if (meta)
            merged_vv.merge(meta->version_vector);
    }
    merged_vv.increment(use_drive_id);

    for (auto& [id, store] : manifests) {
        FileMetadata resolved = *winning_meta;
        resolved.version_vector = merged_vv;
        store->upsert_file(resolved);
    }

    std::cout << "Resolved conflict for '" << path << "' using drive '" << use_drive_id << "'.\n";
    return 0;
}

static int do_verify(const std::vector<std::string>& drive_paths, bool all, const Config& cfg) {
    std::vector<fs::path> targets;
    if (!resolve_drive_targets(all, drive_paths, "verify", targets))
        return 1;

    int exit_code = 0;
    for (const auto& mount : targets) {
        fs::path db_path = manifest_db_path(mount, cfg);
        if (!fs::exists(db_path)) {
            std::cerr << "No manifest found at " << mount.string()
                      << ". Run 'caravault scan' first.\n";
            exit_code = 1;
            continue;
        }

        try {
            auto store = ManifestStore::open(db_path);
            auto drives = store.get_all_drives();
            std::string drive_id = drives.empty() ? mount.filename().string() : drives.front();

            std::cout << "Verifying drive: " << drive_id << "\n";
            int corrupt = 0;
            for (const auto& f : store.get_all_files()) {
                if (f.tombstone)
                    continue;
                fs::path abs_path = mount / fs::path(f.path);
                if (!Executor::read_file_verified(f.path, abs_path, store)) {
                    std::cerr << "  CORRUPT: " << f.path << "\n";
                    ++corrupt;
                    exit_code = 1;
                }
            }
            std::cout << (corrupt == 0 ? "  All files OK.\n"
                                       : "  " + std::to_string(corrupt) + " corrupt file(s).\n");
            store.close();
        } catch (const std::exception& ex) {
            std::cerr << "Error verifying drive at " << mount.string() << ": " << ex.what() << "\n";
            exit_code = 1;
        }
    }
    return exit_code;
}

static int do_config_set(const std::string& kv_pair) {
    auto eq = kv_pair.find('=');
    if (eq == std::string::npos) {
        std::cerr << "Error: --set requires <key>=<value> format.\n";
        return 1;
    }
    std::string key = kv_pair.substr(0, eq);
    std::string value = kv_pair.substr(eq + 1);

    Config cfg = load_config();

    // Apply the single key by writing a one-line temp file and loading it.
    fs::path tmp = fs::temp_directory_path() / "caravault_config_set.conf";
    {
        std::ofstream f(tmp);
        f << key << "=" << value << "\n";
    }
    cfg.load_from_file(tmp);
    fs::remove(tmp);

    if (!cfg.validate())
        std::cerr << "Warning: value was invalid and reset to default.\n";

    cfg.save_to_file(config_path());
    std::cout << "Set " << key << "=" << value << "\n";
    return 0;
}

static int do_sync(bool dry_run,
                   bool verbose,
                   const std::vector<std::string>& drive_paths,
                   bool all,
                   const Config& cfg) {
    std::vector<fs::path> targets;
    if (!resolve_drive_targets(all, drive_paths, "sync", targets))
        return 1;

    std::map<std::string, std::unique_ptr<ManifestStore>> stores_owned;
    std::map<std::string, ManifestStore*> manifests;
    std::map<std::string, fs::path> drive_roots;

    for (const auto& mount : targets) {
        fs::path db_path = manifest_db_path(mount, cfg);
        if (!fs::exists(db_path)) {
            if (verbose)
                std::cout << "No manifest on " << mount.string()
                          << " — run 'caravault scan' first.\n";
            continue;
        }
        try {
            auto store = std::make_unique<ManifestStore>(ManifestStore::open(db_path));
            auto drives = store->get_all_drives();
            std::string id = drives.empty() ? mount.filename().string() : drives.front();
            drive_roots[id] = mount;
            manifests[id] = store.get();
            stores_owned[id] = std::move(store);
        } catch (const std::exception& ex) {
            std::cerr << "Warning: cannot open manifest on " << mount.string() << ": " << ex.what()
                      << "\n";
        }
    }

    if (manifests.size() < 2) {
        std::cout << "Need at least 2 drives with manifests to sync. "
                     "Run 'caravault scan' on each drive first.\n";
        return 0;
    }

    ConflictResolver resolver;
    auto conflicts = resolver.detect_conflicts(manifests);

    if (!conflicts.empty()) {
        std::cout << conflicts.size() << " conflict(s) detected:\n";
        for (const auto& c : conflicts)
            std::cout << "  " << c.path << "\n";
    }

    std::vector<Resolution> resolutions;
    for (const auto& c : conflicts) {
        auto res = resolver.resolve(c, manifests.size());
        if (verbose && !res.winning_drive_id.empty())
            std::cout << "  Resolved '" << c.path << "' -> drive " << res.winning_drive_id << "\n";
        resolutions.push_back(res);
    }

    SyncPlanner planner;
    auto ops = planner.plan_sync(manifests, resolutions);

    if (ops.empty()) {
        std::cout << "All drives are in sync. Nothing to do.\n";
        return 0;
    }

    std::cout << ops.size() << " operation(s) planned.\n";

    if (dry_run) {
        // Print conflicts section
        if (!conflicts.empty()) {
            std::cout << "\n[dry-run] Detected conflicts (" << conflicts.size() << "):\n";
            for (const auto& c : conflicts) {
                std::cout << "  conflict: " << c.path << "\n";
                for (const auto& [drive_id, meta] : c.versions)
                    std::cout << "    drive=" << drive_id << " hash=" << meta.hash << "\n";
                std::cout << "    recommended strategy: "
                          << resolution_strategy_str(c.recommended_strategy) << "\n";
            }
        }

        // Print planned operations grouped by type
        std::cout << "\n[dry-run] Planned operations (no files modified):\n";
        size_t n_copy = 0, n_replace = 0, n_delete = 0, n_rename = 0, n_mkdir = 0;
        for (const auto& op : ops) {
            std::cout << "  " << sync_op_type_str(op.type) << " " << op.path << " ("
                      << op.source_drive_id << " -> " << op.target_drive_id << ")\n";
            switch (op.type) {
                case SyncOpType::COPY:    ++n_copy;    break;
                case SyncOpType::REPLACE: ++n_replace; break;
                case SyncOpType::DELETE:  ++n_delete;  break;
                case SyncOpType::RENAME:  ++n_rename;  break;
                case SyncOpType::MKDIR:   ++n_mkdir;   break;
            }
        }

        // Print summary
        std::cout << "\n[dry-run] Summary: " << n_copy << " copy, " << n_replace << " replace, "
                  << n_delete << " delete, " << n_rename << " rename, " << n_mkdir << " mkdir"
                  << " (total: " << ops.size() << " operation(s))\n";
        return 0;
    }

    Executor executor;
    size_t done = 0;
    int exit_code = 0;

    // Compute totals for progress reporting.
    size_t total_bytes = 0;
    for (const auto& op : ops) {
        if (op.type == SyncOpType::COPY || op.type == SyncOpType::REPLACE) {
            auto src_it = manifests.find(op.source_drive_id);
            if (src_it != manifests.end()) {
                auto meta = src_it->second->get_file(op.path);
                if (meta)
                    total_bytes += meta->size;
            }
        }
    }

    ProgressReporter reporter;
    reporter.start(ops.size(), total_bytes);

    // Planning phase indicator.
    reporter.update(0, "Planning sync operations...");

    size_t files_processed = 0;
    size_t files_copied = 0;
    size_t files_deleted = 0;
    size_t files_renamed = 0;

    auto sync_start = std::chrono::steady_clock::now();

    for (const auto& op : ops) {
        auto result = executor.execute(op, drive_roots, manifests, &reporter, &files_processed);
        ++done;
        if (result.success) {
            switch (op.type) {
                case SyncOpType::COPY:
                case SyncOpType::REPLACE: ++files_copied; break;
                case SyncOpType::DELETE:  ++files_deleted; break;
                case SyncOpType::RENAME:  ++files_renamed; break;
                default: break;
            }
            if (verbose)
                std::cout << "  [" << done << "/" << ops.size() << "] " << sync_op_type_str(op.type)
                          << " " << op.path << "\n";
        } else {
            std::cerr << "  [" << done << "/" << ops.size() << "] FAILED "
                      << sync_op_type_str(op.type) << " " << op.path << ": " << result.error_message
                      << "\n";
            exit_code = 1;
        }
    }

    auto sync_end = std::chrono::steady_clock::now();
    ProgressReporter::SummaryStats stats;
    stats.files_copied = files_copied;
    stats.files_deleted = files_deleted;
    stats.files_renamed = files_renamed;
    stats.conflicts_resolved = resolutions.size();
    stats.bytes_transferred = reporter.bytes_transferred();
    stats.duration_seconds =
        std::chrono::duration<double>(sync_end - sync_start).count();
    reporter.finish(stats);
    reporter.display_summary();

    std::cout << "Sync complete: " << done << " operation(s) performed.\n";
    return exit_code;
}

int main(int argc, char** argv) {
    CLI::App app{"Caravault - Offline Multi-Drive File Synchronization System"};
    app.set_version_flag("--version", "1.0.0");
    app.require_subcommand(0, 1);

    // sync
    auto* sync_cmd = app.add_subcommand("sync", "Synchronize the specified drives");
    bool sync_dry_run = false;
    bool sync_verbose = false;
    bool sync_all = false;
    std::vector<std::string> sync_drives;
    sync_cmd->add_flag(
        "--dry-run", sync_dry_run, "Show planned operations without modifying files");
    sync_cmd->add_flag("--verbose,-v", sync_verbose, "Show detailed progress");
    sync_cmd->add_flag("--all", sync_all, "Sync all auto-detected drives");
    sync_cmd->add_option("--drive", sync_drives, "Mount point path(s) to include in sync")
        ->allow_extra_args(false);

    // status
    auto* status_cmd = app.add_subcommand("status", "Show status of the specified drives");
    bool status_all = false;
    std::vector<std::string> status_drives;
    status_cmd->add_flag("--all", status_all, "Show status for all auto-detected drives");
    status_cmd->add_option("--drive", status_drives, "Mount point path(s) to show status for")
        ->allow_extra_args(false);

    // conflicts
    auto* conflicts_cmd =
        app.add_subcommand("conflicts", "List conflicts detected across the specified drives");
    bool conflicts_all = false;
    std::vector<std::string> conflicts_drives;
    conflicts_cmd->add_flag("--all", conflicts_all, "Check all auto-detected drives");
    conflicts_cmd->add_option("--drive", conflicts_drives, "Mount point path(s) to check")
        ->allow_extra_args(false);

    // resolve
    auto* resolve_cmd =
        app.add_subcommand("resolve", "Manually resolve a conflict for a specific file");
    std::string resolve_path;
    std::string resolve_drive_id;
    bool resolve_all = false;
    std::vector<std::string> resolve_drives;
    resolve_cmd->add_option("path", resolve_path, "Relative file path to resolve")->required();
    resolve_cmd->add_option("--use-drive", resolve_drive_id, "Drive ID to use as the winner")
        ->required();
    resolve_cmd->add_flag("--all", resolve_all, "Apply resolution across all auto-detected drives");
    resolve_cmd->add_option("--drive", resolve_drives, "Mount point path(s) to apply resolution to")
        ->allow_extra_args(false);

    // scan
    auto* scan_cmd = app.add_subcommand("scan", "Scan drives and build/update manifests");
    bool scan_all = false;
    std::vector<std::string> scan_drives;
    scan_cmd->add_flag("--all", scan_all, "Scan all auto-detected drives");
    scan_cmd->add_option("--drive", scan_drives, "Mount point path(s) to scan")
        ->allow_extra_args(false);

    // verify
    auto* verify_cmd =
        app.add_subcommand("verify", "Verify data integrity of the specified drives");
    bool verify_all = false;
    std::vector<std::string> verify_drives;
    verify_cmd->add_flag("--all", verify_all, "Verify all auto-detected drives");
    verify_cmd->add_option("--drive", verify_drives, "Mount point path(s) to verify")
        ->allow_extra_args(false);

    // config
    auto* config_cmd = app.add_subcommand("config", "View or modify configuration settings");
    std::string config_set_kv;
    config_cmd->add_option("--set", config_set_kv, "Set a configuration value (key=value)")
        ->required();

    CLI11_PARSE(app, argc, argv);

    if (app.get_subcommands().empty()) {
        std::cout << app.help() << "\n";
        return 0;
    }

    Config cfg = load_config();

    if (app.got_subcommand("sync"))
        return do_sync(sync_dry_run, sync_verbose, sync_drives, sync_all, cfg);
    if (app.got_subcommand("status"))
        return do_status(status_drives, status_all, cfg);
    if (app.got_subcommand("conflicts"))
        return do_conflicts(conflicts_drives, conflicts_all, cfg);
    if (app.got_subcommand("resolve"))
        return do_resolve(resolve_path, resolve_drive_id, resolve_drives, resolve_all, cfg);
    if (app.got_subcommand("scan"))
        return do_scan(scan_drives, scan_all, cfg);
    if (app.got_subcommand("verify"))
        return do_verify(verify_drives, verify_all, cfg);
    if (app.got_subcommand("config"))
        return do_config_set(config_set_kv);

    return 0;
}
