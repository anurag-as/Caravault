#include "bloom_filter.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace caravault {

namespace {

// m = -n * ln(p) / ln(2)^2
size_t optimal_bit_count(size_t n, double p) {
    if (n == 0)
        return 1;
    return static_cast<size_t>(
        std::ceil(-static_cast<double>(n) * std::log(p) / (std::log(2.0) * std::log(2.0))));
}

// k = (m/n) * ln(2)
size_t optimal_hash_count(size_t m, size_t n) {
    if (n == 0)
        return 1;
    return std::max(size_t{1},
                    static_cast<size_t>(std::round(static_cast<double>(m) / static_cast<double>(n) *
                                                   std::log(2.0))));
}

}  // namespace

BloomFilter::BloomFilter(size_t expected_elements, double false_positive_rate) {
    if (false_positive_rate <= 0.0 || false_positive_rate >= 1.0) {
        throw std::invalid_argument("false_positive_rate must be in (0, 1)");
    }
    size_t m = optimal_bit_count(expected_elements, false_positive_rate);
    num_hash_functions_ = optimal_hash_count(m, expected_elements);
    bits_.assign(m, false);
}

size_t BloomFilter::hash_position(const std::string& item, size_t seed) const {
    constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;

    uint64_t h = FNV_OFFSET ^ (seed * 2654435761ULL);
    for (unsigned char c : item) {
        h ^= static_cast<uint64_t>(c);
        h *= FNV_PRIME;
    }
    return static_cast<size_t>(h % bits_.size());
}

void BloomFilter::insert(const std::string& item) {
    for (size_t i = 0; i < num_hash_functions_; ++i)
        bits_[hash_position(item, i)] = true;
}

bool BloomFilter::might_contain(const std::string& item) const {
    for (size_t i = 0; i < num_hash_functions_; ++i) {
        if (!bits_[hash_position(item, i)])
            return false;
    }
    return true;
}

void BloomFilter::clear() {
    std::fill(bits_.begin(), bits_.end(), false);
}

size_t BloomFilter::size_bytes() const {
    return (bits_.size() + 7) / 8;
}

}  // namespace caravault
