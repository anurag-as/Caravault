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

    explicit VersionVector(const std::map<std::string, uint64_t>& clocks);

    void increment(const std::string& drive_id);

    /**
     * Merge another version vector into this one by taking element-wise maximum.
     * After merge, this vector will dominate or equal both inputs.
     */
    void merge(const VersionVector& other);

    /**
     * Compare this version vector with another to determine causal ordering.
     *
     * - DOMINATES:    ∀ drive: this[drive] >= other[drive] AND ∃ drive: this[drive] > other[drive]
     * - DOMINATED_BY: other DOMINATES this
     * - CONCURRENT:   ∃ drive_a: this[drive_a] > other[drive_a] AND ∃ drive_b: other[drive_b] > this[drive_b]
     * - EQUAL:        ∀ drive: this[drive] == other[drive]
     */
    Ordering compare(const VersionVector& other) const;

    uint64_t get_clock(const std::string& drive_id) const;
    const std::map<std::string, uint64_t>& get_clocks() const;

    std::string to_json() const;

    /**
     * @throws std::runtime_error if JSON is malformed
     */
    static VersionVector from_json(const std::string& json);

    bool operator==(const VersionVector& other) const;

private:
    std::map<std::string, uint64_t> clocks_;
};

} // namespace caravault

#endif // CARAVAULT_VERSION_VECTOR_HPP
