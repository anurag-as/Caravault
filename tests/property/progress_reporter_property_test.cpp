#include "progress_reporter.hpp"

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>

using namespace caravault;

namespace {

rc::Gen<std::string> gen_filename() {
    return rc::gen::map(rc::gen::string<std::string>(), [](std::string s) {
        std::string out;
        for (char c : s)
            if (std::isalnum(static_cast<unsigned char>(c)))
                out += c;
        return out.empty() ? std::string("file") : out;
    });
}

}  // namespace

// For any sync session, the reporter SHALL track the number of files processed
// and the total number of files to process.
RC_GTEST_PROP(ProgressReporterProperty, ProgressReportingDuringSync, ()) {
    int n = *rc::gen::inRange<int>(1, 20);
    int k = *rc::gen::inRange<int>(1, n + 1);

    ProgressReporter reporter;
    reporter.start(static_cast<size_t>(n), 0);

    for (int i = 1; i <= k; ++i)
        reporter.update(static_cast<size_t>(i), "op");

    RC_ASSERT(reporter.files_processed() == static_cast<size_t>(k));
    RC_ASSERT(reporter.total_files() == static_cast<size_t>(n));

    reporter.finish({});
}

// For any update call, the reporter SHALL reflect the current operation string.
RC_GTEST_PROP(ProgressReporterProperty, CurrentOperationReporting, ()) {
    std::string op_string = "Copying " + *gen_filename();

    ProgressReporter reporter;
    reporter.start(10, 0);
    reporter.update(1, op_string);

    RC_ASSERT(reporter.current_operation() == op_string);

    reporter.finish({});
}

// For any sequence of byte updates, the reporter SHALL track cumulative bytes
// transferred and SHALL NOT exceed the declared total.
RC_GTEST_PROP(ProgressReporterProperty, LargeFileTransferProgress, ()) {
    int num_chunks = *rc::gen::inRange<int>(1, 11);
    auto chunk_sizes = *rc::gen::container<std::vector<size_t>>(static_cast<size_t>(num_chunks),
                                                                rc::gen::inRange<size_t>(1, 65537));

    size_t total = 0;
    for (size_t s : chunk_sizes)
        total += s;

    ProgressReporter reporter;
    reporter.start(1, total);

    size_t cumulative = 0;
    for (size_t s : chunk_sizes) {
        cumulative += s;
        reporter.update_bytes(cumulative);
    }

    RC_ASSERT(reporter.bytes_transferred() == total);
    RC_ASSERT(reporter.bytes_transferred() <= reporter.total_bytes());

    reporter.finish({});
}
