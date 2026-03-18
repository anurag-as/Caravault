#include "sync_planner.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace caravault {

namespace {

std::string parent_dir(const std::string& path) {
    auto pos = path.rfind('/');
    return (pos == std::string::npos) ? "" : path.substr(0, pos);
}

std::vector<std::string> ancestor_dirs(const std::string& path) {
    std::vector<std::string> dirs;
    for (std::string cur = parent_dir(path); !cur.empty(); cur = parent_dir(cur))
        dirs.push_back(cur);
    return dirs;
}

}  // namespace

std::map<std::string, std::map<std::string, std::string>> SyncPlanner::detect_renames(
    const std::map<std::string, std::vector<FileMetadata>>& drive_files) {
    struct DriveIndex {
        std::map<std::string, std::vector<std::string>> hash_to_paths;
        std::set<std::string> live_paths;
        std::set<std::string> tombstones;
    };

    std::map<std::string, DriveIndex> indices;
    for (const auto& [drive_id, files] : drive_files) {
        DriveIndex& idx = indices[drive_id];
        for (const auto& f : files) {
            if (f.tombstone) {
                idx.tombstones.insert(f.path);
            } else {
                idx.hash_to_paths[f.hash].push_back(f.path);
                idx.live_paths.insert(f.path);
            }
        }
    }

    // A rename on tgt is detected when:
    //   - src has the file at src_path (live)
    //   - tgt does NOT have src_path live (the new name is absent on tgt)
    //   - tgt has the same hash at tgt_path (the old name, still live on tgt)
    //   - tgt_path is tombstoned on src (src explicitly deleted the old name)
    std::map<std::string, std::map<std::string, std::string>> result;

    for (const auto& [src_id, src_idx] : indices) {
        for (const auto& [tgt_id, tgt_idx] : indices) {
            if (src_id == tgt_id)
                continue;

            for (const auto& [hash, src_paths] : src_idx.hash_to_paths) {
                auto tgt_it = tgt_idx.hash_to_paths.find(hash);
                if (tgt_it == tgt_idx.hash_to_paths.end())
                    continue;

                for (const auto& src_path : src_paths) {
                    if (tgt_idx.live_paths.count(src_path))
                        continue;

                    for (const auto& tgt_path : tgt_it->second) {
                        if (tgt_path == src_path)
                            continue;
                        // The old path must be tombstoned on the source drive,
                        // confirming it was intentionally renamed (not just absent).
                        if (!src_idx.tombstones.count(tgt_path))
                            continue;

                        result[tgt_id][tgt_path] = src_path;
                    }
                }
            }
        }
    }

    return result;
}

std::vector<SyncOp> SyncPlanner::topological_sort(std::vector<SyncOp> ops) {
    const size_t n = ops.size();
    std::vector<std::vector<size_t>> adj(n);
    std::vector<size_t> in_degree(n, 0);

    std::map<std::pair<std::string, std::string>, std::vector<size_t>> path_index;
    for (size_t i = 0; i < n; ++i)
        path_index[{ops[i].target_drive_id, ops[i].path}].push_back(i);

    auto add_edge = [&](size_t from, size_t to) {
        adj[from].push_back(to);
        ++in_degree[to];
    };

    for (size_t i = 0; i < n; ++i) {
        const SyncOp& op = ops[i];

        if (op.type == SyncOpType::MKDIR) {
            for (size_t j = 0; j < n; ++j) {
                if (i == j || ops[j].target_drive_id != op.target_drive_id)
                    continue;
                const std::string& jp = ops[j].path;
                if (jp.size() > op.path.size() + 1 && jp.substr(0, op.path.size()) == op.path &&
                    jp[op.path.size()] == '/')
                    add_edge(i, j);
            }
        }

        if (op.type == SyncOpType::RENAME && op.new_path) {
            auto it = path_index.find({op.target_drive_id, *op.new_path});
            if (it != path_index.end())
                for (size_t j : it->second)
                    if (i != j)
                        add_edge(i, j);
        }

        if (op.type == SyncOpType::COPY || op.type == SyncOpType::REPLACE) {
            auto it = path_index.find({op.target_drive_id, op.path});
            if (it != path_index.end())
                for (size_t j : it->second)
                    if (i != j && ops[j].type == SyncOpType::REMOVE)
                        add_edge(i, j);
        }
    }

    std::vector<size_t> queue;
    queue.reserve(n);
    for (size_t i = 0; i < n; ++i)
        if (in_degree[i] == 0)
            queue.push_back(i);

    std::vector<SyncOp> sorted;
    sorted.reserve(n);
    for (size_t head = 0; head < queue.size(); ++head) {
        size_t i = queue[head];
        sorted.push_back(ops[i]);
        for (size_t j : adj[i])
            if (--in_degree[j] == 0)
                queue.push_back(j);
    }

    // Cycle guard: append any remaining nodes (should not occur with valid input).
    if (sorted.size() < n)
        for (size_t i = 0; i < n; ++i)
            if (in_degree[i] != 0)
                sorted.push_back(ops[i]);

    return sorted;
}

std::vector<SyncOp> SyncPlanner::coalesce(std::vector<SyncOp> ops) {
    // Iterate in reverse; keep only the last op for each (target_drive, path).
    std::set<std::pair<std::string, std::string>> seen;
    std::vector<SyncOp> result;
    result.reserve(ops.size());

    for (auto it = ops.rbegin(); it != ops.rend(); ++it)
        if (seen.insert({it->target_drive_id, it->path}).second)
            result.push_back(*it);

    std::reverse(result.begin(), result.end());
    return result;
}

std::vector<SyncOp> SyncPlanner::plan_sync(
    const std::map<std::string, ManifestStore*>& drive_manifests,
    const std::vector<Resolution>& resolutions) {
    if (drive_manifests.empty())
        return {};

    std::map<std::string, std::vector<FileMetadata>> drive_files;
    for (const auto& [drive_id, store_ptr] : drive_manifests)
        if (store_ptr)
            drive_files[drive_id] = store_ptr->get_all_files();

    std::map<std::string, std::map<std::string, FileMetadata>> file_index;
    for (const auto& [drive_id, files] : drive_files)
        for (const auto& f : files)
            file_index[drive_id][f.path] = f;

    std::set<std::string> all_paths;
    for (const auto& [drive_id, files] : drive_files)
        for (const auto& f : files)
            all_paths.insert(f.path);

    std::vector<std::string> all_drive_ids;
    for (const auto& [id, _] : drive_manifests)
        all_drive_ids.push_back(id);

    auto rename_maps = detect_renames(drive_files);

    std::vector<SyncOp> ops;
    std::set<std::pair<std::string, std::string>> mkdir_emitted;

    auto emit_mkdirs = [&](const std::string& path, const std::string& tgt) {
        for (const auto& dir : ancestor_dirs(path)) {
            if (mkdir_emitted.insert({tgt, dir}).second) {
                SyncOp op;
                op.type = SyncOpType::MKDIR;
                op.target_drive_id = tgt;
                op.path = dir;
                ops.push_back(std::move(op));
            }
        }
    };

    // Emit RENAME ops and mark those paths as handled.
    std::set<std::pair<std::string, std::string>> rename_handled;

    for (const auto& [tgt_drive, rename_map] : rename_maps) {
        for (const auto& [old_path, new_path] : rename_map) {
            std::string src_drive;
            FileMetadata src_meta;
            for (const auto& [did, fidx] : file_index) {
                if (did == tgt_drive)
                    continue;
                auto fit = fidx.find(new_path);
                if (fit != fidx.end() && !fit->second.tombstone) {
                    src_drive = did;
                    src_meta = fit->second;
                    break;
                }
            }
            if (src_drive.empty())
                continue;

            VersionVector merged_vv = src_meta.version_vector;
            for (const auto& [did, fidx] : file_index) {
                for (const auto& p : {old_path, new_path})
                    if (auto fit = fidx.find(p); fit != fidx.end())
                        merged_vv.merge(fit->second.version_vector);
            }

            emit_mkdirs(new_path, tgt_drive);

            SyncOp op;
            op.type = SyncOpType::RENAME;
            op.source_drive_id = src_drive;
            op.target_drive_id = tgt_drive;
            op.path = old_path;
            op.new_path = new_path;
            op.new_version_vector = merged_vv;
            ops.push_back(std::move(op));

            rename_handled.insert({tgt_drive, old_path});
            rename_handled.insert({tgt_drive, new_path});
        }
    }

    for (const auto& path : all_paths) {
        // If >50% of all registered drives have tombstone=true for this file,
        // propagate DELETE to all drives that still have the file as live.
        if (!all_drive_ids.empty()) {
            size_t tombstone_count = 0;
            std::string tombstone_src;
            VersionVector tombstone_vv;
            for (const auto& [drive_id, fidx] : file_index) {
                auto fit = fidx.find(path);
                if (fit != fidx.end() && fit->second.tombstone) {
                    ++tombstone_count;
                    if (tombstone_src.empty()) {
                        tombstone_src = drive_id;
                        tombstone_vv = fit->second.version_vector;
                    } else {
                        tombstone_vv.merge(fit->second.version_vector);
                    }
                }
            }
            if (tombstone_count * 2 > all_drive_ids.size()) {
                // Quorum of tombstones: propagate deletion to all live drives.
                for (const auto& tgt_drive : all_drive_ids) {
                    if (rename_handled.count({tgt_drive, path}))
                        continue;
                    auto& tgt_fidx = file_index[tgt_drive];
                    auto tgt_it = tgt_fidx.find(path);
                    if (tgt_it != tgt_fidx.end() && !tgt_it->second.tombstone) {
                        SyncOp op;
                        op.type = SyncOpType::REMOVE;
                        op.source_drive_id = tombstone_src;
                        op.target_drive_id = tgt_drive;
                        op.path = path;
                        op.new_version_vector = tombstone_vv;
                        ops.push_back(std::move(op));
                    }
                }
                continue;  // Skip normal per-file planning for this path.
            }
        }

        // Prefer a conflict resolution winner; otherwise pick the dominant version.
        std::string src_drive;
        FileMetadata src_meta;
        bool found = false;

        for (const auto& res : resolutions) {
            if (res.winning_drive_id.empty())
                continue;
            auto& idx = file_index[res.winning_drive_id];
            auto fit = idx.find(path);
            if (fit == idx.end())
                continue;

            for (const auto& [did, fidx] : file_index) {
                if (did == res.winning_drive_id)
                    continue;
                auto oit = fidx.find(path);
                if (oit == fidx.end())
                    continue;
                if (fit->second.version_vector.compare(oit->second.version_vector) ==
                    VersionVector::Ordering::CONCURRENT) {
                    src_drive = res.winning_drive_id;
                    src_meta = fit->second;
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }

        if (!found) {
            for (const auto& [drive_id, fidx] : file_index) {
                auto fit = fidx.find(path);
                if (fit == fidx.end())
                    continue;
                if (!found) {
                    src_drive = drive_id;
                    src_meta = fit->second;
                    found = true;
                } else if (fit->second.version_vector.compare(src_meta.version_vector) ==
                           VersionVector::Ordering::DOMINATES) {
                    src_drive = drive_id;
                    src_meta = fit->second;
                }
            }
        }

        if (!found)
            continue;

        VersionVector merged_vv = src_meta.version_vector;
        for (const auto& [drive_id, fidx] : file_index)
            if (auto fit = fidx.find(path); fit != fidx.end())
                merged_vv.merge(fit->second.version_vector);

        for (const auto& tgt_drive : all_drive_ids) {
            if (tgt_drive == src_drive)
                continue;
            if (rename_handled.count({tgt_drive, path}))
                continue;

            auto& tgt_fidx = file_index[tgt_drive];
            auto tgt_it = tgt_fidx.find(path);

            if (src_meta.tombstone) {
                if (tgt_it != tgt_fidx.end() && !tgt_it->second.tombstone) {
                    SyncOp op;
                    op.type = SyncOpType::REMOVE;
                    op.source_drive_id = src_drive;
                    op.target_drive_id = tgt_drive;
                    op.path = path;
                    op.new_version_vector = merged_vv;
                    ops.push_back(std::move(op));
                }
                continue;
            }

            if (tgt_it == tgt_fidx.end() || tgt_it->second.tombstone) {
                emit_mkdirs(path, tgt_drive);
                SyncOp op;
                op.type = SyncOpType::COPY;
                op.source_drive_id = src_drive;
                op.target_drive_id = tgt_drive;
                op.path = path;
                op.new_version_vector = merged_vv;
                ops.push_back(std::move(op));
            } else {
                auto ord = src_meta.version_vector.compare(tgt_it->second.version_vector);
                if (ord == VersionVector::Ordering::DOMINATES ||
                    src_meta.hash != tgt_it->second.hash) {
                    SyncOp op;
                    op.type = SyncOpType::REPLACE;
                    op.source_drive_id = src_drive;
                    op.target_drive_id = tgt_drive;
                    op.path = path;
                    op.new_version_vector = merged_vv;
                    ops.push_back(std::move(op));
                }
            }
        }
    }

    ops = topological_sort(std::move(ops));
    ops = coalesce(std::move(ops));
    return ops;
}

}  // namespace caravault
