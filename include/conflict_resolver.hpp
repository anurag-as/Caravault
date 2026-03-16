#ifndef CARAVAULT_CONFLICT_RESOLVER_HPP
#define CARAVAULT_CONFLICT_RESOLVER_HPP

#include "manifest_store.hpp"

#include <map>
#include <string>
#include <vector>

namespace caravault {

enum class ResolutionStrategy {
    DOMINANT_VERSION,
    MAJORITY_QUORUM,
    TOMBSTONE_WINS,
    LAST_WRITE_WINS,
    COPY_ALL_VERSIONS,
    MANUAL_RESOLUTION,
};

struct ConflictInfo {
    std::string path;
    std::map<std::string, FileMetadata> versions;  // drive_id -> metadata
    ResolutionStrategy recommended_strategy;
};

struct Resolution {
    std::string winning_drive_id;  // empty for COPY_ALL_VERSIONS / MANUAL_RESOLUTION
    ResolutionStrategy strategy_used;
    std::vector<std::string> actions;
};

/**
 * Detects files with concurrent modifications across drives and applies
 * resolution strategies to select a winning version.
 *
 * Resolution order: DOMINANT_VERSION → MAJORITY_QUORUM → TOMBSTONE_WINS
 *                   → LAST_WRITE_WINS → COPY_ALL_VERSIONS → MANUAL_RESOLUTION
 */
class ConflictResolver {
public:
    /**
     * Return all files that have concurrent (conflicting) version vectors
     * across the given drive manifests. Causally ordered updates are not
     * reported as conflicts.
     */
    std::vector<ConflictInfo> detect_conflicts(
        const std::map<std::string, ManifestStore*>& drive_manifests);

    /**
     * Apply resolution strategies to a single conflict.
     * @param total_drives  Total registered drives, used for quorum threshold.
     */
    Resolution resolve(const ConflictInfo& conflict, size_t total_drives);
};

}  // namespace caravault

#endif  // CARAVAULT_CONFLICT_RESOLVER_HPP
