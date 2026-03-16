#include "bloom_filter.hpp"

#include <gtest/gtest.h>
#include <string>
#include <unordered_set>

using namespace caravault;

// Construction

TEST(BloomFilterTest, ConstructsWithoutError) {
    EXPECT_NO_THROW(BloomFilter(1000, 0.01));
    EXPECT_NO_THROW(BloomFilter(0, 0.01));
}

TEST(BloomFilterTest, InvalidFalsePositiveRateThrows) {
    EXPECT_THROW(BloomFilter(1000, 0.0), std::invalid_argument);
    EXPECT_THROW(BloomFilter(1000, 1.0), std::invalid_argument);
    EXPECT_THROW(BloomFilter(1000, -0.5), std::invalid_argument);
    EXPECT_THROW(BloomFilter(1000, 1.5), std::invalid_argument);
}

TEST(BloomFilterTest, SizeBytesIsPositive) {
    BloomFilter bf(1000, 0.01);
    EXPECT_GT(bf.size_bytes(), size_t{0});
}

// No false negatives: inserted items must always be found

TEST(BloomFilterTest, InsertedItemsAlwaysFound) {
    BloomFilter bf(500, 0.01);
    std::vector<std::string> items;
    for (int i = 0; i < 500; ++i)
        items.push_back("path/to/file_" + std::to_string(i) + ".txt");
    for (const auto& item : items)
        bf.insert(item);
    for (const auto& item : items)
        EXPECT_TRUE(bf.might_contain(item)) << "False negative for: " << item;
}

TEST(BloomFilterTest, EmptyFilterContainsNothing) {
    BloomFilter bf(1000, 0.01);
    EXPECT_FALSE(bf.might_contain("anything"));
    EXPECT_FALSE(bf.might_contain(""));
    EXPECT_FALSE(bf.might_contain("some/path/file.txt"));
}

// False positive rate stays within bounds

TEST(BloomFilterTest, FalsePositiveRateWithinBounds) {
    const size_t n_insert = 1000;
    const size_t n_test = 10000;
    const double allowed_fpr = 0.05;  // 5x target; avoids flakiness

    BloomFilter bf(n_insert, 0.01);
    std::unordered_set<std::string> inserted;
    for (size_t i = 0; i < n_insert; ++i) {
        std::string s = "inserted_" + std::to_string(i);
        bf.insert(s);
        inserted.insert(s);
    }

    size_t false_positives = 0;
    for (size_t i = 0; i < n_test; ++i) {
        std::string s = "query_" + std::to_string(i);
        if (inserted.count(s) == 0 && bf.might_contain(s))
            ++false_positives;
    }

    double actual_fpr = static_cast<double>(false_positives) / static_cast<double>(n_test);
    EXPECT_LE(actual_fpr, allowed_fpr)
        << "False positive rate " << actual_fpr << " exceeds allowed " << allowed_fpr;
}

// clear() resets filter state

TEST(BloomFilterTest, ClearResetsState) {
    BloomFilter bf(100, 0.01);
    bf.insert("file_a.txt");
    bf.insert("file_b.txt");
    EXPECT_TRUE(bf.might_contain("file_a.txt"));
    EXPECT_TRUE(bf.might_contain("file_b.txt"));

    bf.clear();

    EXPECT_FALSE(bf.might_contain("file_a.txt"));
    EXPECT_FALSE(bf.might_contain("file_b.txt"));
}

TEST(BloomFilterTest, ClearAllowsReuse) {
    BloomFilter bf(100, 0.01);
    bf.insert("old_item");
    bf.clear();
    bf.insert("new_item");
    EXPECT_TRUE(bf.might_contain("new_item"));
    EXPECT_FALSE(bf.might_contain("old_item"));
}

TEST(BloomFilterTest, SizeBytesUnchangedAfterClear) {
    BloomFilter bf(1000, 0.01);
    size_t before = bf.size_bytes();
    bf.insert("x");
    bf.clear();
    EXPECT_EQ(bf.size_bytes(), before);
}
