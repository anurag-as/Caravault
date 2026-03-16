#include "conflict_resolver.hpp"

#include <map>
#include <string>
#include <vector>

namespace caravault {

std::vector<ConflictInfo> ConflictResolver::detect_conflicts(
    const std::map<std::string, ManifestStore*>& drive_manifests) {
    std::map<std::string, std::map<std::string, FileMetadata>> all_versions;
    for (const auto& [drive_id, store_ptr] : drive_manifests) {
        if (!store_ptr)
            continue;
        for (const auto& meta : store_ptr->get_all_files())
            all_versions[meta.path][drive_id] = meta;
    }

    std::vector<ConflictInfo> conflicts;
    for (auto& [path, versions] : all_versions) {
        if (versions.size() < 2)
            continue;

        bool has_concurrent = false;
        for (auto it = versions.begin(); it != versions.end() && !has_concurrent; ++it) {
            for (auto jt = std::next(it); jt != versions.end() && !has_concurrent; ++jt) {
                if (it->second.version_vector.compare(jt->second.version_vector) ==
                    VersionVector::Ordering::CONCURRENT)
                    has_concurrent = true;
            }
        }
        if (!has_concurrent)
            continue;

        conflicts.push_back({path, versions, ResolutionStrategy::MANUAL_RESOLUTION});
    }
    return conflicts;
}

Resolution ConflictResolver::resolve(const ConflictInfo& conflict, size_t total_drives) {
    const auto& versions = conflict.versions;

    // 1. DOMINANT_VERSION: one version vector dominates all others
    for (const auto& [candidate_id, candidate_meta] : versions) {
        bool dominates_all = true;
        for (const auto& [other_id, other_meta] : versions) {
            if (other_id == candidate_id)
                continue;
            auto ord = candidate_meta.version_vector.compare(other_meta.version_vector);
            if (ord != VersionVector::Ordering::DOMINATES &&
                ord != VersionVector::Ordering::EQUAL) {
                dominates_all = false;
                break;
            }
        }
        if (dominates_all)
            return {candidate_id,
                    ResolutionStrategy::DOMINANT_VERSION,
                    {"Selected dominant version from drive " + candidate_id}};
    }

    // 2. MAJORITY_QUORUM: >50% of all registered drives share the same hash
    if (total_drives > 0) {
        std::map<std::string, std::vector<std::string>> hash_to_drives;
        for (const auto& [drive_id, meta] : versions) {
            if (!meta.tombstone)
                hash_to_drives[meta.hash].push_back(drive_id);
        }
        for (const auto& [hash, drives] : hash_to_drives) {
            if (drives.size() * 2 > total_drives) {
                VersionVector merged = versions.at(drives.front()).version_vector;
                for (const auto& [drive_id, meta] : versions)
                    merged.merge(meta.version_vector);
                return {drives.front(),
                        ResolutionStrategy::MAJORITY_QUORUM,
                        {"Quorum: " + std::to_string(drives.size()) + "/" +
                             std::to_string(total_drives) + " drives agree on " + hash,
                         "Version vectors merged to reflect resolution"}};
            }
        }
    }

    // 3. TOMBSTONE_WINS: any version is a tombstone
    for (const auto& [drive_id, meta] : versions) {
        if (meta.tombstone)
            return {drive_id,
                    ResolutionStrategy::TOMBSTONE_WINS,
                    {"Tombstone from drive " + drive_id + " wins for " + conflict.path}};
    }

    // 4. LAST_WRITE_WINS: highest mtime
    std::string best_drive;
    uint64_t best_mtime = 0;
    for (const auto& [drive_id, meta] : versions) {
        if (meta.mtime > best_mtime) {
            best_mtime = meta.mtime;
            best_drive = drive_id;
        }
    }
    if (!best_drive.empty())
        return {best_drive,
                ResolutionStrategy::LAST_WRITE_WINS,
                {"Last-write-wins: drive " + best_drive + " mtime=" + std::to_string(best_mtime)}};

    // 5. COPY_ALL_VERSIONS: fallback
    return {
        "", ResolutionStrategy::COPY_ALL_VERSIONS, {"Preserving all versions of " + conflict.path}};
}

}  // namespace caravault
