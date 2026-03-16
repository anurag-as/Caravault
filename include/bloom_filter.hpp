#ifndef CARAVAULT_BLOOM_FILTER_HPP
#define CARAVAULT_BLOOM_FILTER_HPP

#include <cstddef>
#include <string>
#include <vector>

namespace caravault {

/**
 * Probabilistic set membership filter with no false negatives.
 *
 * Bit array size and hash function count are derived automatically from
 * expected_elements and false_positive_rate using the standard optimal formulas.
 */
class BloomFilter {
public:
    /**
     * Construct a filter sized for the expected number of elements and desired
     * false positive rate.
     *
     * Throws std::invalid_argument if false_positive_rate is not in (0, 1).
     */
    BloomFilter(size_t expected_elements, double false_positive_rate);

    void insert(const std::string& item);

    /**
     * Returns false if the item is definitely not in the set.
     * Returns true if the item is probably in the set (false positives possible).
     */
    bool might_contain(const std::string& item) const;

    void clear();

    size_t size_bytes() const;

private:
    std::vector<bool> bits_;
    size_t num_hash_functions_;

    size_t hash_position(const std::string& item, size_t seed) const;
};

}  // namespace caravault

#endif  // CARAVAULT_BLOOM_FILTER_HPP
