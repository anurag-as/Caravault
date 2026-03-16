#include "version_vector.hpp"

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

using namespace caravault;

// For any file in the manifest, there SHALL exist an associated version vector.
// This property verifies that VersionVector objects can be constructed and maintain
// their clock data, ensuring every file can have an associated version vector.
RC_GTEST_PROP(VersionVectorProperty, VersionVectorPresence, ()) {
    // Generate arbitrary clock map by building it from a vector of pairs
    auto clock_pairs = *rc::gen::container<std::vector<std::pair<std::string, uint64_t>>>(
        rc::gen::pair(rc::gen::string<std::string>(), rc::gen::inRange<uint64_t>(0, 1000)));

    std::map<std::string, uint64_t> clocks;
    for (const auto& [drive_id, clock] : clock_pairs) {
        clocks[drive_id] = clock;
    }

    // Construct version vector from clocks
    VersionVector vv(clocks);

    // Verify all clocks are present and accessible
    for (const auto& [drive_id, expected_clock] : clocks) {
        RC_ASSERT(vv.get_clock(drive_id) == expected_clock);
    }

    // Verify the version vector maintains all clocks
    RC_ASSERT(vv.get_clocks().size() == clocks.size());
}

// For any file modification on a drive, the version vector's logical clock
// for that drive SHALL increase.
RC_GTEST_PROP(VersionVectorProperty, VersionVectorClockIncrement, ()) {
    // Generate initial version vector
    auto clock_pairs = *rc::gen::container<std::vector<std::pair<std::string, uint64_t>>>(
        rc::gen::pair(rc::gen::string<std::string>(), rc::gen::inRange<uint64_t>(0, 1000)));

    std::map<std::string, uint64_t> initial_clocks;
    for (const auto& [drive_id, clock] : clock_pairs) {
        initial_clocks[drive_id] = clock;
    }

    VersionVector vv(initial_clocks);

    // Generate a drive ID to increment (may or may not exist in initial clocks)
    auto drive_id = *rc::gen::string<std::string>();

    // Get clock value before increment
    uint64_t clock_before = vv.get_clock(drive_id);

    // Increment the clock
    vv.increment(drive_id);

    // Get clock value after increment
    uint64_t clock_after = vv.get_clock(drive_id);

    // Verify clock increased by exactly 1
    RC_ASSERT(clock_after == clock_before + 1);

    // Verify other clocks remain unchanged
    for (const auto& [other_drive_id, expected_clock] : initial_clocks) {
        if (other_drive_id != drive_id) {
            RC_ASSERT(vv.get_clock(other_drive_id) == expected_clock);
        }
    }
}

// For any two version vectors V1 and V2:
// - V1 DOMINATES V2 iff ∀ drive: V1[drive] >= V2[drive] AND ∃ drive: V1[drive] > V2[drive]
// - V1 CONCURRENT V2 iff ∃ drive_a: V1[drive_a] > V2[drive_a] AND ∃ drive_b: V2[drive_b] >
// V1[drive_b]
// - V1 EQUAL V2 iff ∀ drive: V1[drive] == V2[drive]
RC_GTEST_PROP(VersionVectorProperty, VersionVectorCausalityComparison, ()) {
    // Generate two version vectors with overlapping drive IDs
    auto drive_ids = *rc::gen::container<std::vector<std::string>>(rc::gen::string<std::string>());

    // Ensure we have at least one drive ID for meaningful comparison
    RC_PRE(!drive_ids.empty());

    // Generate clocks for both vectors
    std::map<std::string, uint64_t> clocks1, clocks2;
    for (const auto& drive_id : drive_ids) {
        clocks1[drive_id] = *rc::gen::inRange<uint64_t>(0, 100);
        clocks2[drive_id] = *rc::gen::inRange<uint64_t>(0, 100);
    }

    VersionVector vv1(clocks1);
    VersionVector vv2(clocks2);

    // Compute expected ordering manually
    bool all_greater_or_equal = true;
    bool some_strictly_greater = false;
    bool some_strictly_less = false;

    for (const auto& drive_id : drive_ids) {
        uint64_t c1 = vv1.get_clock(drive_id);
        uint64_t c2 = vv2.get_clock(drive_id);

        if (c1 < c2) {
            all_greater_or_equal = false;
            some_strictly_less = true;
        } else if (c1 > c2) {
            some_strictly_greater = true;
        }
    }

    // Determine expected ordering
    VersionVector::Ordering expected;
    if (!some_strictly_greater && !some_strictly_less) {
        expected = VersionVector::Ordering::EQUAL;
    } else if (all_greater_or_equal && some_strictly_greater) {
        expected = VersionVector::Ordering::DOMINATES;
    } else if (!all_greater_or_equal && !some_strictly_greater) {
        expected = VersionVector::Ordering::DOMINATED_BY;
    } else {
        expected = VersionVector::Ordering::CONCURRENT;
    }

    // Verify actual ordering matches expected
    VersionVector::Ordering actual = vv1.compare(vv2);
    RC_ASSERT(actual == expected);

    // Verify symmetry properties
    VersionVector::Ordering reverse = vv2.compare(vv1);
    if (actual == VersionVector::Ordering::DOMINATES) {
        RC_ASSERT(reverse == VersionVector::Ordering::DOMINATED_BY);
    } else if (actual == VersionVector::Ordering::DOMINATED_BY) {
        RC_ASSERT(reverse == VersionVector::Ordering::DOMINATES);
    } else if (actual == VersionVector::Ordering::EQUAL) {
        RC_ASSERT(reverse == VersionVector::Ordering::EQUAL);
    } else if (actual == VersionVector::Ordering::CONCURRENT) {
        RC_ASSERT(reverse == VersionVector::Ordering::CONCURRENT);
    }
}

// For any version vector, storing it to the database and reloading it SHALL
// produce an equal version vector. This tests JSON serialization round-trip.
RC_GTEST_PROP(VersionVectorProperty, VersionVectorPersistenceRoundTrip, ()) {
    // Generate arbitrary version vector
    auto clock_pairs = *rc::gen::container<std::vector<std::pair<std::string, uint64_t>>>(
        rc::gen::pair(rc::gen::string<std::string>(), rc::gen::inRange<uint64_t>(0, UINT64_MAX)));

    std::map<std::string, uint64_t> clocks;
    for (const auto& [drive_id, clock] : clock_pairs) {
        clocks[drive_id] = clock;
    }

    VersionVector original(clocks);

    // Serialize to JSON
    std::string json = original.to_json();

    // Deserialize from JSON
    VersionVector restored = VersionVector::from_json(json);

    // Verify equality
    RC_ASSERT(original == restored);

    // Verify comparison returns EQUAL
    RC_ASSERT(original.compare(restored) == VersionVector::Ordering::EQUAL);
}

// This verifies that merge takes element-wise maximum and produces a dominating vector
RC_GTEST_PROP(VersionVectorProperty, MergeProducesMaximum, ()) {
    // Generate two version vectors
    auto clock_pairs1 = *rc::gen::container<std::vector<std::pair<std::string, uint64_t>>>(
        rc::gen::pair(rc::gen::string<std::string>(), rc::gen::inRange<uint64_t>(0, 1000)));
    auto clock_pairs2 = *rc::gen::container<std::vector<std::pair<std::string, uint64_t>>>(
        rc::gen::pair(rc::gen::string<std::string>(), rc::gen::inRange<uint64_t>(0, 1000)));

    std::map<std::string, uint64_t> clocks1, clocks2;
    for (const auto& [drive_id, clock] : clock_pairs1) {
        clocks1[drive_id] = clock;
    }
    for (const auto& [drive_id, clock] : clock_pairs2) {
        clocks2[drive_id] = clock;
    }

    VersionVector vv1(clocks1);
    VersionVector vv2(clocks2);

    // Create copies for comparison
    VersionVector vv1_copy(clocks1);
    VersionVector vv2_copy(clocks2);

    // Merge vv2 into vv1
    vv1.merge(vv2);

    // Collect all drive IDs
    std::set<std::string> all_drives;
    for (const auto& [drive_id, _] : clocks1) {
        all_drives.insert(drive_id);
    }
    for (const auto& [drive_id, _] : clocks2) {
        all_drives.insert(drive_id);
    }

    // Verify merged vector has element-wise maximum
    for (const auto& drive_id : all_drives) {
        uint64_t c1 = vv1_copy.get_clock(drive_id);
        uint64_t c2 = vv2_copy.get_clock(drive_id);
        uint64_t expected_max = std::max(c1, c2);
        RC_ASSERT(vv1.get_clock(drive_id) == expected_max);
    }

    // Verify merged vector dominates or equals both inputs
    auto ord1 = vv1.compare(vv1_copy);
    auto ord2 = vv1.compare(vv2_copy);
    RC_ASSERT(ord1 == VersionVector::Ordering::DOMINATES || ord1 == VersionVector::Ordering::EQUAL);
    RC_ASSERT(ord2 == VersionVector::Ordering::DOMINATES || ord2 == VersionVector::Ordering::EQUAL);
}

// Increment is idempotent in effect (multiple increments increase clock)
RC_GTEST_PROP(VersionVectorProperty, MultipleIncrementsAccumulate, ()) {
    VersionVector vv;
    auto drive_id = *rc::gen::string<std::string>();
    auto num_increments = *rc::gen::inRange<size_t>(1, 100);

    // Perform multiple increments
    for (size_t i = 0; i < num_increments; ++i) {
        vv.increment(drive_id);
    }

    // Verify clock equals number of increments
    RC_ASSERT(vv.get_clock(drive_id) == num_increments);
}
