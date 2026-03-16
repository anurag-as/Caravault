#include "version_vector.hpp"

#include <gtest/gtest.h>

using namespace caravault;

// Test default constructor creates empty version vector
TEST(VersionVectorTest, DefaultConstructorCreatesEmptyVector) {
    VersionVector vv;
    EXPECT_EQ(vv.get_clock("drive1"), 0);
    EXPECT_EQ(vv.get_clock("drive2"), 0);
    EXPECT_TRUE(vv.get_clocks().empty());
}

// Test constructor with initial clocks
TEST(VersionVectorTest, ConstructorWithClocks) {
    std::map<std::string, uint64_t> clocks = {{"drive1", 5}, {"drive2", 3}};
    VersionVector vv(clocks);

    EXPECT_EQ(vv.get_clock("drive1"), 5);
    EXPECT_EQ(vv.get_clock("drive2"), 3);
    EXPECT_EQ(vv.get_clock("drive3"), 0);  // Missing drive returns 0
}

// Test increment operation
TEST(VersionVectorTest, IncrementIncreasesClockForDrive) {
    VersionVector vv;

    vv.increment("drive1");
    EXPECT_EQ(vv.get_clock("drive1"), 1);

    vv.increment("drive1");
    EXPECT_EQ(vv.get_clock("drive1"), 2);

    vv.increment("drive2");
    EXPECT_EQ(vv.get_clock("drive2"), 1);
    EXPECT_EQ(vv.get_clock("drive1"), 2);  // drive1 unchanged
}

// Test merge operation
TEST(VersionVectorTest, MergeTakesElementWiseMaximum) {
    VersionVector vv1({{"drive1", 5}, {"drive2", 2}});
    VersionVector vv2({{"drive1", 3}, {"drive2", 7}, {"drive3", 4}});

    vv1.merge(vv2);

    EXPECT_EQ(vv1.get_clock("drive1"), 5);  // max(5, 3)
    EXPECT_EQ(vv1.get_clock("drive2"), 7);  // max(2, 7)
    EXPECT_EQ(vv1.get_clock("drive3"), 4);  // max(0, 4)
}

// Test compare: EQUAL
TEST(VersionVectorTest, CompareReturnsEqualForIdenticalVectors) {
    VersionVector vv1({{"drive1", 5}, {"drive2", 3}});
    VersionVector vv2({{"drive1", 5}, {"drive2", 3}});

    EXPECT_EQ(vv1.compare(vv2), VersionVector::Ordering::EQUAL);
    EXPECT_EQ(vv2.compare(vv1), VersionVector::Ordering::EQUAL);
}

// Test compare: DOMINATES
TEST(VersionVectorTest, CompareReturnsDominatesWhenAllClocksGreaterOrEqual) {
    VersionVector vv1({{"drive1", 5}, {"drive2", 3}});
    VersionVector vv2({{"drive1", 4}, {"drive2", 3}});

    EXPECT_EQ(vv1.compare(vv2), VersionVector::Ordering::DOMINATES);
    EXPECT_EQ(vv2.compare(vv1), VersionVector::Ordering::DOMINATED_BY);
}

// Test compare: CONCURRENT
TEST(VersionVectorTest, CompareReturnsConcurrentForConflictingVectors) {
    VersionVector vv1({{"drive1", 5}, {"drive2", 2}});
    VersionVector vv2({{"drive1", 3}, {"drive2", 7}});

    EXPECT_EQ(vv1.compare(vv2), VersionVector::Ordering::CONCURRENT);
    EXPECT_EQ(vv2.compare(vv1), VersionVector::Ordering::CONCURRENT);
}

// Test compare with missing drives (treated as 0)
TEST(VersionVectorTest, CompareHandlesMissingDrivesAsZero) {
    VersionVector vv1({{"drive1", 5}});
    VersionVector vv2({{"drive1", 5}, {"drive2", 3}});

    // vv2 dominates vv1 because drive2: 3 > 0
    EXPECT_EQ(vv2.compare(vv1), VersionVector::Ordering::DOMINATES);
    EXPECT_EQ(vv1.compare(vv2), VersionVector::Ordering::DOMINATED_BY);
}

// Test JSON serialization
TEST(VersionVectorTest, ToJsonSerializesCorrectly) {
    VersionVector vv({{"drive1", 5}, {"drive2", 3}});
    std::string json = vv.to_json();

    // JSON should contain both entries (order may vary due to map)
    EXPECT_NE(json.find("\"drive1\":5"), std::string::npos);
    EXPECT_NE(json.find("\"drive2\":3"), std::string::npos);
    EXPECT_EQ(json.front(), '{');
    EXPECT_EQ(json.back(), '}');
}

// Test JSON deserialization
TEST(VersionVectorTest, FromJsonDeserializesCorrectly) {
    std::string json = R"({"drive1":5,"drive2":3})";
    VersionVector vv = VersionVector::from_json(json);

    EXPECT_EQ(vv.get_clock("drive1"), 5);
    EXPECT_EQ(vv.get_clock("drive2"), 3);
}

// Test JSON round-trip
TEST(VersionVectorTest, JsonRoundTripPreservesData) {
    VersionVector original({{"drive1", 5}, {"drive2", 3}, {"drive3", 10}});
    std::string json = original.to_json();
    VersionVector deserialized = VersionVector::from_json(json);

    EXPECT_EQ(original, deserialized);
}

// Test JSON with empty vector
TEST(VersionVectorTest, EmptyVectorJsonRoundTrip) {
    VersionVector empty;
    std::string json = empty.to_json();
    EXPECT_EQ(json, "{}");

    VersionVector deserialized = VersionVector::from_json(json);
    EXPECT_EQ(empty, deserialized);
}

// Test JSON with escaped characters
TEST(VersionVectorTest, JsonHandlesEscapedCharacters) {
    VersionVector vv({{R"(drive"1)", 5}, {R"(drive\2)", 3}});
    std::string json = vv.to_json();
    VersionVector deserialized = VersionVector::from_json(json);

    EXPECT_EQ(vv, deserialized);
}

// Test equality operators
TEST(VersionVectorTest, EqualityOperators) {
    VersionVector vv1({{"drive1", 5}, {"drive2", 3}});
    VersionVector vv2({{"drive1", 5}, {"drive2", 3}});
    VersionVector vv3({{"drive1", 5}, {"drive2", 4}});

    EXPECT_TRUE(vv1 == vv2);
    EXPECT_FALSE(vv1 != vv2);
    EXPECT_FALSE(vv1 == vv3);
    EXPECT_TRUE(vv1 != vv3);
}

// Test invalid JSON handling
TEST(VersionVectorTest, FromJsonThrowsOnInvalidJson) {
    EXPECT_THROW(VersionVector::from_json(""), std::runtime_error);
    EXPECT_THROW(VersionVector::from_json("not json"), std::runtime_error);
    EXPECT_THROW(VersionVector::from_json("{\"key\":}"), std::runtime_error);
    EXPECT_THROW(VersionVector::from_json("{\"key\""), std::runtime_error);
}
