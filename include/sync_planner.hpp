#ifndef CARAVAULT_SYNC_PLANNER_HPP
#define CARAVAULT_SYNC_PLANNER_HPP

#include "conflict_resolver.hpp"
#include "manifest_store.hpp"
#include "version_vector.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace caravault {

enum class SyncOpType {
    COPY,
    REPLACE,
    REMOVE,
    RENAME,
    MKDIR,
};

struct SyncOp {
    SyncOpType type;
    std::string source_drive_id;
    std::string target_drive_id;
    std::string path;
    std::optional<std::string> new_path;
    VersionVector new_version_vector;
};

/**
 * Generates an ordered sequence of SyncOps from drive manifests and resolved conflicts.
 *
 * Planning steps:
 *  1. Collect all file states across drives.
 *  2. Detect renames by matching content hashes.
 *  3. Generate MKDIR, COPY, REPLACE, DELETE, and RENAME operations.
 *  4. Topologically sort operations to respect dependencies.
 *  5. Coalesce redundant operations.
 */
class SyncPlanner {
public:
    std::vector<SyncOp> plan_sync(const std::map<std::string, ManifestStore*>& drive_manifests,
                                  const std::vector<Resolution>& resolutions);

private:
    std::map<std::string, std::map<std::string, std::string>> detect_renames(
        const std::map<std::string, std::vector<FileMetadata>>& drive_files);

    std::vector<SyncOp> topological_sort(std::vector<SyncOp> ops);
    std::vector<SyncOp> coalesce(std::vector<SyncOp> ops);
};

}  // namespace caravault

#endif  // CARAVAULT_SYNC_PLANNER_HPP
