#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define popen _popen
#define pclose _pclose
#define getpid _getpid
#else
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

struct CmdResult {
    int exit_code = 0;
    std::string stdout_output;
    std::string stderr_output;
};

// CMake injects CARAVAULT_BINARY_PATH as an absolute path at compile time.
static fs::path find_binary() {
#ifdef CARAVAULT_BINARY_PATH
    fs::path injected{CARAVAULT_BINARY_PATH};
    if (fs::exists(injected))
        return injected;
#endif
    for (const auto* candidate : {"./caravault", "../caravault", "caravault"}) {
        fs::path p{candidate};
        if (fs::exists(p))
            return p;
    }
    return fs::path("caravault");
}

static CmdResult run_cmd(const std::string& cmd) {
    CmdResult result;

    // Redirect stderr to a temp file; use the result address for uniqueness.
    fs::path stderr_tmp =
        fs::temp_directory_path() /
        ("caravault_cli_stderr_" + std::to_string(reinterpret_cast<uintptr_t>(&result)) + ".txt");

    FILE* pipe = popen((cmd + " 2>" + stderr_tmp.string()).c_str(), "r");
    if (!pipe) {
        result.exit_code = -1;
        return result;
    }

    char buf[256];
    while (fgets(buf, sizeof(buf), pipe) != nullptr)
        result.stdout_output += buf;

    result.exit_code = pclose(pipe);
#if !defined(_WIN32)
    if (WIFEXITED(result.exit_code))
        result.exit_code = WEXITSTATUS(result.exit_code);
#endif

    if (fs::exists(stderr_tmp)) {
        std::ifstream f(stderr_tmp);
        std::ostringstream ss;
        ss << f.rdbuf();
        result.stderr_output = ss.str();
        fs::remove(stderr_tmp);
    }

    return result;
}

static std::string binary_path() {
    return find_binary().string();
}

static const std::vector<std::string> kSubcommands = {
    "sync",
    "status",
    "conflicts",
    "resolve",
    "scan",
    "verify",
    "config",
};

// Commands that require --all or --drive <path> and must exit non-zero without them.
static const std::vector<std::string> kDriveSelectCommands = {
    "sync",
    "status",
    "conflicts",
    "scan",
    "verify",
};

}  // namespace

// For any invocation with --help or a subcommand --help, the output SHALL
// contain the name of every supported subcommand.
RC_GTEST_PROP(CLIProperty, HelpTextCompleteness, ()) {
    int choice = *rc::gen::inRange<int>(-1, static_cast<int>(kSubcommands.size()));

    std::string cmd = binary_path();
    if (choice == -1) {
        cmd += " --help";
    } else {
        cmd += " " + kSubcommands[static_cast<size_t>(choice)] + " --help";
    }

    auto result = run_cmd(cmd);
    RC_ASSERT(result.exit_code == 0);

    if (choice == -1) {
        for (const auto& sub : kSubcommands)
            RC_ASSERT(result.stdout_output.find(sub) != std::string::npos);
    } else {
        const auto& sub = kSubcommands[static_cast<size_t>(choice)];
        RC_ASSERT(result.stdout_output.find(sub) != std::string::npos);
    }
}

// For any invocation with an unknown subcommand, the exit code SHALL be
// non-zero and some error output SHALL be produced.
RC_GTEST_PROP(CLIProperty, CommandFailureExitCode, ()) {
    auto raw = *rc::gen::map(rc::gen::container<std::string>(rc::gen::inRange<char>('a', 'z' + 1)),
                             [](std::string s) { return s.empty() ? std::string("zzz") : s; });

    bool is_real = false;
    for (const auto& c : kSubcommands)
        if (raw == c) {
            is_real = true;
            break;
        }
    RC_PRE(!is_real);
    RC_PRE(!raw.empty());

    auto result = run_cmd(binary_path() + " " + raw);
    RC_ASSERT(result.exit_code != 0);
    RC_ASSERT(!result.stdout_output.empty() || !result.stderr_output.empty());
}

TEST(CLITest, HelpExitsZero) {
    auto result = run_cmd(binary_path() + " --help");
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_FALSE(result.stdout_output.empty());
}

TEST(CLITest, VersionExitsZero) {
    auto result = run_cmd(binary_path() + " --version");
    EXPECT_EQ(result.exit_code, 0);
}

TEST(CLITest, UnknownSubcommandExitsNonZero) {
    auto result = run_cmd(binary_path() + " nonexistent_command_xyz");
    EXPECT_NE(result.exit_code, 0);
}

TEST(CLITest, ScanNoFlagsExitsNonZero) {
    auto result = run_cmd(binary_path() + " scan");
    EXPECT_NE(result.exit_code, 0);
}

// Every drive-selection command must exit non-zero when neither --all nor --drive is given.
TEST(CLITest, DriveSelectCommandsRequireDriveFlag) {
    for (const auto& cmd : kDriveSelectCommands) {
        auto result = run_cmd(binary_path() + " " + cmd);
        EXPECT_NE(result.exit_code, 0) << cmd << " should exit non-zero without --all or --drive";
        bool has_error = !result.stdout_output.empty() || !result.stderr_output.empty();
        EXPECT_TRUE(has_error) << cmd << " should print an error message";
    }
}

// resolve also requires --all or --drive (plus its positional and --use-drive args).
TEST(CLITest, ResolveRequiresDriveFlag) {
    auto result = run_cmd(binary_path() + " resolve some/file.txt --use-drive drive-A");
    EXPECT_NE(result.exit_code, 0);
}

// --all flag is accepted by every drive-selection command (may exit 0 or non-zero
// depending on whether drives are present, but must not crash or print usage errors).
TEST(CLITest, DriveSelectCommandsAcceptAllFlag) {
    for (const auto& cmd : kDriveSelectCommands) {
        auto result = run_cmd(binary_path() + " " + cmd + " --all");
        // Exit code may be 0 (no drives) or non-zero (error), but the output must
        // not contain the "specify --all or --drive" usage error.
        bool usage_error =
            result.stderr_output.find("specify --all or --drive") != std::string::npos;
        EXPECT_FALSE(usage_error) << cmd << " --all should not produce a usage error";
    }
}

// --drive flag is accepted by every drive-selection command.
TEST(CLITest, DriveSelectCommandsAcceptDriveFlag) {
    for (const auto& cmd : kDriveSelectCommands) {
        auto result = run_cmd(binary_path() + " " + cmd + " --drive /nonexistent/path");
        bool usage_error =
            result.stderr_output.find("specify --all or --drive") != std::string::npos;
        EXPECT_FALSE(usage_error) << cmd << " --drive should not produce a usage error";
    }
}

TEST(CLITest, ScanHelpExitsZero) {
    auto result = run_cmd(binary_path() + " scan --help");
    EXPECT_EQ(result.exit_code, 0);
    EXPECT_NE(result.stdout_output.find("scan"), std::string::npos);
}

TEST(CLITest, AllSubcommandsInTopLevelHelp) {
    auto result = run_cmd(binary_path() + " --help");
    EXPECT_EQ(result.exit_code, 0);
    for (const auto& s : kSubcommands)
        EXPECT_NE(result.stdout_output.find(s), std::string::npos)
            << "Missing subcommand in help: " << s;
}

// When dry-run mode is enabled, the system SHALL NOT modify any files or metadata.
TEST(CLIProperty, DryRunNoModification) {
    fs::path drive1 =
        fs::temp_directory_path() / ("caravault_dryrun80_d1_" + std::to_string(::getpid()));
    fs::path drive2 =
        fs::temp_directory_path() / ("caravault_dryrun80_d2_" + std::to_string(::getpid()));
    fs::create_directories(drive1);
    fs::create_directories(drive2);

    // Write test files
    {
        std::ofstream f(drive1 / "file_a.txt");
        f << "content on drive1\n";
    }
    {
        std::ofstream f(drive2 / "file_b.txt");
        f << "content on drive2\n";
    }

    // Scan both drives to create manifests
    run_cmd(binary_path() + " scan --drive " + drive1.string());
    run_cmd(binary_path() + " scan --drive " + drive2.string());

    // Record mtimes of all files AFTER scan (scan legitimately modifies manifest.db)
    // Exclude SQLite WAL auxiliary files (manifest.db-shm, manifest.db-wal) which SQLite
    // touches on any database open, even read-only
    auto is_sqlite_aux = [](const fs::path& p) {
        auto name = p.filename().string();
        return name.size() > 4 &&
               (name.substr(name.size() - 4) == "-shm" || name.substr(name.size() - 4) == "-wal");
    };
    std::map<fs::path, fs::file_time_type> before_mtimes;
    for (const auto& entry : fs::recursive_directory_iterator(drive1)) {
        if (!entry.is_regular_file() || is_sqlite_aux(entry.path()))
            continue;
        before_mtimes[entry.path()] = entry.last_write_time();
    }
    for (const auto& entry : fs::recursive_directory_iterator(drive2)) {
        if (!entry.is_regular_file() || is_sqlite_aux(entry.path()))
            continue;
        before_mtimes[entry.path()] = entry.last_write_time();
    }

    // Small sleep to ensure any modification would show a different mtime
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Run sync --dry-run — must not modify any files
    auto result = run_cmd(binary_path() + " sync --dry-run --drive " + drive1.string() +
                          " --drive " + drive2.string());

    // The binary should not crash
    EXPECT_NE(result.exit_code, 139) << "Binary crashed (SIGSEGV)";

    // Verify no files were modified (mtimes unchanged)
    for (const auto& [path, mtime] : before_mtimes) {
        if (fs::exists(path)) {
            EXPECT_EQ(fs::last_write_time(path), mtime)
                << "File was modified during dry-run: " << path;
        }
    }

    // Cleanup
    fs::remove_all(drive1);
    fs::remove_all(drive2);
}

// When running in dry-run mode, the system SHALL display all files that would
// be copied, modified, or deleted.
TEST(CLIProperty, DryRunOperationReporting) {
    fs::path drive1 =
        fs::temp_directory_path() / ("caravault_dryrun81_d1_" + std::to_string(::getpid()));
    fs::path drive2 =
        fs::temp_directory_path() / ("caravault_dryrun81_d2_" + std::to_string(::getpid()));
    fs::create_directories(drive1);
    fs::create_directories(drive2);

    // Write a file only on drive1
    {
        std::ofstream f(drive1 / "unique_file.txt");
        f << "only on drive1\n";
    }

    // Scan both drives
    run_cmd(binary_path() + " scan --drive " + drive1.string());
    run_cmd(binary_path() + " scan --drive " + drive2.string());

    // Run sync --dry-run on both drives
    auto result = run_cmd(binary_path() + " sync --dry-run --drive " + drive1.string() +
                          " --drive " + drive2.string());

    // Should not crash
    EXPECT_NE(result.exit_code, 139) << "Binary crashed";

    // stdout must contain dry-run indicator
    std::string out_lower = result.stdout_output;
    std::transform(out_lower.begin(), out_lower.end(), out_lower.begin(), ::tolower);
    EXPECT_NE(out_lower.find("dry"), std::string::npos)
        << "Expected dry-run indicator in output. Got: " << result.stdout_output;

    // stdout must contain operation type keywords or "planned"
    bool has_op = out_lower.find("copy") != std::string::npos ||
                  out_lower.find("replace") != std::string::npos ||
                  out_lower.find("delete") != std::string::npos ||
                  out_lower.find("planned") != std::string::npos ||
                  out_lower.find("operation") != std::string::npos;
    EXPECT_TRUE(has_op) << "Expected operation keywords in output. Got: " << result.stdout_output;

    // Cleanup
    fs::remove_all(drive1);
    fs::remove_all(drive2);
}

// When running in dry-run mode, the system SHALL display all detected conflicts.
TEST(CLIProperty, DryRunConflictReporting) {
    fs::path drive1 =
        fs::temp_directory_path() / ("caravault_dryrun82_d1_" + std::to_string(::getpid()));
    fs::path drive2 =
        fs::temp_directory_path() / ("caravault_dryrun82_d2_" + std::to_string(::getpid()));
    fs::create_directories(drive1);
    fs::create_directories(drive2);

    // Write the same path with different content on each drive
    {
        std::ofstream f(drive1 / "shared.txt");
        f << "version from drive1\n";
    }
    {
        std::ofstream f(drive2 / "shared.txt");
        f << "version from drive2 - different content\n";
    }

    // Scan each drive independently
    run_cmd(binary_path() + " scan --drive " + drive1.string());
    run_cmd(binary_path() + " scan --drive " + drive2.string());

    // Use sqlite3 to set concurrent version vectors for shared.txt on each drive.
    // Drive1 gets {"driveA":1}, drive2 gets {"driveB":1} — these are CONCURRENT.
    fs::path db1 = drive1 / ".caravault" / "manifest.db";
    fs::path db2 = drive2 / ".caravault" / "manifest.db";
    run_cmd("sqlite3 " + db1.string() +
            " \"UPDATE files SET version_vector='{\\\"driveA\\\":1}' WHERE path='shared.txt'\"");
    run_cmd("sqlite3 " + db2.string() +
            " \"UPDATE files SET version_vector='{\\\"driveB\\\":1}' WHERE path='shared.txt'\"");

    // Run sync --dry-run
    auto result = run_cmd(binary_path() + " sync --dry-run --drive " + drive1.string() +
                          " --drive " + drive2.string());

    // Should not crash
    EXPECT_NE(result.exit_code, 139) << "Binary crashed";

    // stdout must contain conflict information
    std::string out_lower = result.stdout_output;
    std::transform(out_lower.begin(), out_lower.end(), out_lower.begin(), ::tolower);
    EXPECT_NE(out_lower.find("conflict"), std::string::npos)
        << "Expected conflict information in output. Got: " << result.stdout_output;

    // Cleanup
    fs::remove_all(drive1);
    fs::remove_all(drive2);
}
