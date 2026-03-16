#ifndef CARAVAULT_VERSION_VECTOR_HPP
#define CARAVAULT_VERSION_VECTOR_HPP

#include <cstdint>
#include <map>
#include <string>

namespace caravault {

/**
 * VersionVector tracks causality relationships between file versions across drives.
 * 
 * Each drive maintains a logical clock that increments with each modification.
 * Version vectors enable detection of concurrent modifications (conflicts) by
 * comparing the causal ordering of file versions.
 * 
 * Missing drive IDs are treated as having clock value 0.
 */
class VersionVector {
public:
    /**
     * Ordering relationship between two version vectors.
     * 
     * DOMINATES: This version causally precedes the other (this is newer)
     * DOMINATED_BY: The other version causally precedes this (other is newer)
     * CONCURRENT: Neither causally precedes the other (conflict)
     * EQUAL: Both versions are identical
     */
    enum class Ordering {
        DOMINATES,
        DOMINATED_BY,
        CONCURRENT,
        EQUAL
    };

    /**
     * Construct an empty version vector with all clocks at 0.
     */
    VersionVector();

    /**
     * Construct a version vector from an existing clock map.
     * 
     * @param clocks Map of drive_id -> logical_clock values
     */
    explicit VersionVector(const std::map<std::string, uint64_t>& clocks);

    /**
     * Increment the logical clock for the specified drive.
     * 
     * @param drive_id The drive identifier whose clock to increment
     */
    void increment(const std::string& drive_id);

    /**
     * Merge another version vector into this one by taking element-wise maximum.
     * 
     * After merge, this version vector will dominate or equal both input vectors.
     * 
     * @param other The version vector to merge
     */
    void merge(const VersionVector& other);

    /**
     * Compare this version vector with another to determine causal ordering.
     * 
     * Comparison logic:
     * - DOMINATES: ∀ drive: this[drive] >= other[drive] AND ∃ drive: this[drive] > other[drive]
     * - DOMINATED_BY: other DOMINATES this
     * - CONCURRENT: ∃ drive_a: this[drive_a] > other[drive_a] AND ∃ drive_b: other[drive_b] > this[drive_b]
     * - EQUAL: ∀ drive: this[drive] == other[drive]
     * 
     * @param other The version vector to compare against
     * @return The ordering relationship
     */
    Ordering compare(const VersionVector& other) const;

    /**
     * Get the logical clock value for a specific drive.
     * 
     * @param drive_id The drive identifier
     * @return The clock value (0 if drive not present)
     */
    uint64_t get_clock(const std::string& drive_id) const;

    /**
     * Get all clocks in this version vector.
     * 
     * @return Const reference to the internal clock map
     */
    const std::map<std::string, uint64_t>& get_clocks() const;

    /**
     * Serialize this version vector to JSON format.
     * 
     * Format: {"drive_id1": clock1, "drive_id2": clock2, ...}
     * 
     * @return JSON string representation
     */
    std::string to_json() const;

    /**
     * Deserialize a version vector from JSON format.
     * 
     * @param json JSON string representation
     * @return Deserialized version vector
     * @throws std::runtime_error if JSON is malformed
     */
    static VersionVector from_json(const std::string& json);

    /**
     * Equality comparison operator.
     */
    bool operator==(const VersionVector& other) const;

    /**
     * Inequality comparison operator.
     */
    bool operator!=(const VersionVector& other) const;

private:
    std::map<std::string, uint64_t> clocks_;
};

} // namespace caravault

#endif // CARAVAULT_VERSION_VECTOR_HPP
