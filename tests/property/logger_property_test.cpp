#include "conflict_resolver.hpp"
#include "logger.hpp"
#include "version_vector.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

fs::path make_temp_log_path() {
    static std::atomic<int> counter{0};
    return fs::temp_directory_path() /
           ("caravault_log_prop_" + std::to_string(counter.fetch_add(1)) + ".log");
}

std::vector<std::string> read_lines(const fs::path& p) {
    std::vector<std::string> lines;
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line))
        lines.push_back(line);
    return lines;
}

bool file_contains(const fs::path& p, const std::string& needle) {
    for (const auto& line : read_lines(p))
        if (line.find(needle) != std::string::npos)
            return true;
    return false;
}

struct TempLog {
    fs::path path;
    Logger logger;

    explicit TempLog(Config::LogLevel level = Config::LogLevel::DEBUG,
                     size_t rotation_size = 10 * 1024 * 1024)
        : path(make_temp_log_path()), logger(path, level, rotation_size) {}

    ~TempLog() {
        std::error_code ec;
        auto stem = path.stem().string();
        for (auto& entry : fs::directory_iterator(path.parent_path(), ec)) {
            auto name = entry.path().filename().string();
            if (name.rfind(stem, 0) == 0)
                fs::remove(entry.path());
        }
    }
};

FileMetadata make_meta(const std::string& path,
                       const std::string& hash,
                       uint64_t mtime,
                       const VersionVector& vv) {
    FileMetadata m;
    m.path = path;
    m.hash = hash;
    m.size = 0;
    m.mtime = mtime;
    m.version_vector = vv;
    return m;
}

rc::Gen<std::string> gen_nonempty_string() {
    return rc::gen::map(rc::gen::string<std::string>(), [](std::string s) {
        s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == '\n' || c == '\r'; }),
                s.end());
        return s.empty() ? std::string("x") : s;
    });
}

}  // namespace

// For any detected conflict, the log SHALL contain the conflict with timestamp,
// file path, and all involved drive identifiers.
RC_GTEST_PROP(LoggerProperty, ConflictLoggingCompleteness, ()) {
    auto path = *gen_nonempty_string();
    auto num_drives = *rc::gen::inRange<size_t>(2, 5);

    std::map<std::string, FileMetadata> versions;
    std::vector<std::string> drive_ids;
    for (size_t i = 0; i < num_drives; ++i) {
        std::string drive_id = "drive_" + std::to_string(i);
        drive_ids.push_back(drive_id);
        VersionVector vv;
        vv.increment(drive_id);
        versions[drive_id] = make_meta(path, "hash_" + std::to_string(i), 100 + i, vv);
    }

    ConflictInfo conflict{path, versions, ResolutionStrategy::MANUAL_RESOLUTION};

    TempLog tl;
    tl.logger.log_conflict(conflict);

    RC_ASSERT(file_contains(tl.path, path));
    for (const auto& drive_id : drive_ids)
        RC_ASSERT(file_contains(tl.path, drive_id));
}

// For any quorum-based resolution, the log SHALL contain the winning version
// hash and the list of drives that voted for it.
RC_GTEST_PROP(LoggerProperty, QuorumResolutionLogging, ()) {
    auto path = *gen_nonempty_string();
    auto winning_hash = *gen_nonempty_string();
    auto num_voters = *rc::gen::inRange<size_t>(1, 5);

    std::vector<std::string> voters;
    for (size_t i = 0; i < num_voters; ++i)
        voters.push_back("voter_" + std::to_string(i));

    TempLog tl;
    tl.logger.log_quorum_resolution(path, winning_hash, voters);

    RC_ASSERT(file_contains(tl.path, path));
    RC_ASSERT(file_contains(tl.path, winning_hash));
    for (const auto& v : voters)
        RC_ASSERT(file_contains(tl.path, v));
}

// For any sync operation, the log SHALL record the operation type, path,
// source drive, and target drive.
RC_GTEST_PROP(LoggerProperty, SynchronizationOperationLogging, ()) {
    auto operation = *rc::gen::element(std::string("COPY"),
                                       std::string("REPLACE"),
                                       std::string("DELETE"),
                                       std::string("RENAME"),
                                       std::string("MKDIR"));
    auto path = *gen_nonempty_string();
    auto src =
        *rc::gen::map(rc::gen::inRange(0, 100), [](int n) { return "drive_" + std::to_string(n); });
    auto dst =
        *rc::gen::map(rc::gen::inRange(0, 100), [](int n) { return "drive_" + std::to_string(n); });

    TempLog tl;
    tl.logger.log_sync_operation(operation, path, src, dst);

    RC_ASSERT(file_contains(tl.path, operation));
    RC_ASSERT(file_contains(tl.path, path));
    RC_ASSERT(file_contains(tl.path, src));
    RC_ASSERT(file_contains(tl.path, dst));
}

// For any corruption event, the log SHALL record the file path, expected hash,
// and actual hash at ERROR level.
RC_GTEST_PROP(LoggerProperty, CorruptionDetectionLogging, ()) {
    auto path = *gen_nonempty_string();
    auto expected = *gen_nonempty_string();
    auto actual = *gen_nonempty_string();

    TempLog tl;
    tl.logger.log_corruption(path, expected, actual);

    RC_ASSERT(file_contains(tl.path, path));
    RC_ASSERT(file_contains(tl.path, expected));
    RC_ASSERT(file_contains(tl.path, actual));
    RC_ASSERT(file_contains(tl.path, "ERROR"));
}

// For any error, the log SHALL contain the context and message at ERROR level.
RC_GTEST_PROP(LoggerProperty, ErrorLoggingCompleteness, ()) {
    auto context = *gen_nonempty_string();
    auto message = *gen_nonempty_string();

    TempLog tl;
    tl.logger.log_error(context, message);

    RC_ASSERT(file_contains(tl.path, context));
    RC_ASSERT(file_contains(tl.path, message));
    RC_ASSERT(file_contains(tl.path, "ERROR"));
}

// When DEBUG level is set, debug messages SHALL appear. When a higher level is
// set, debug messages SHALL NOT appear.
RC_GTEST_PROP(LoggerProperty, VerboseModeLogging, ()) {
    auto message = *gen_nonempty_string();

    {
        TempLog tl(Config::LogLevel::DEBUG);
        tl.logger.debug(message);
        RC_ASSERT(file_contains(tl.path, message));
    }
    {
        TempLog tl(Config::LogLevel::INFO);
        tl.logger.debug(message);
        RC_ASSERT(!file_contains(tl.path, message));
    }
}

// Messages above the configured level SHALL be filtered out; messages at or
// below it SHALL appear.
RC_GTEST_PROP(LoggerProperty, LogLevelFiltering, ()) {
    static std::atomic<int> seq{0};
    int id = seq.fetch_add(1);
    std::string err_msg = "ERR_" + std::to_string(id);
    std::string warn_msg = "WARN_" + std::to_string(id);
    std::string info_msg = "INFO_" + std::to_string(id);
    std::string dbg_msg = "DBG_" + std::to_string(id);

    TempLog tl(Config::LogLevel::WARNING);
    tl.logger.error(err_msg);
    tl.logger.warning(warn_msg);
    tl.logger.info(info_msg);
    tl.logger.debug(dbg_msg);

    RC_ASSERT(file_contains(tl.path, err_msg));
    RC_ASSERT(file_contains(tl.path, warn_msg));
    RC_ASSERT(!file_contains(tl.path, info_msg));
    RC_ASSERT(!file_contains(tl.path, dbg_msg));
}

// When the log file exceeds the configured rotation size, the system SHALL
// rotate the log file and continue writing to a new file.
RC_GTEST_PROP(LoggerProperty, LogFileRotation, ()) {
    size_t rotation_size = *rc::gen::inRange<size_t>(32, 128);

    TempLog tl(Config::LogLevel::DEBUG, rotation_size);

    std::string big_msg(rotation_size + 1, 'X');
    tl.logger.info(big_msg);
    tl.logger.info("after_rotation_marker");

    RC_ASSERT(tl.logger.rotation_count() >= size_t(1));
    RC_ASSERT(file_contains(tl.path, "after_rotation_marker"));
}

// The log SHALL NOT contain sensitive values (e.g. encryption keys) that were
// never passed to any log method.
RC_GTEST_PROP(LoggerProperty, SensitiveInformationSanitization, ()) {
    std::string sensitive_key = "SECRET_KEY_" + *gen_nonempty_string();

    TempLog tl;
    tl.logger.log_sync_operation("COPY", "file.txt", "drive_a", "drive_b");

    VersionVector vv;
    vv.increment("drive_a");
    ConflictInfo conflict{"file.txt",
                          {{"drive_a", make_meta("file.txt", "hash_a", 100, vv)}},
                          ResolutionStrategy::MANUAL_RESOLUTION};
    tl.logger.log_conflict(conflict);

    RC_ASSERT(!file_contains(tl.path, sensitive_key));
}
