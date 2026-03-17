#ifndef CARAVAULT_LOGGER_HPP
#define CARAVAULT_LOGGER_HPP

#include "config.hpp"
#include "conflict_resolver.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace caravault {

namespace fs = std::filesystem;

/**
 * Level-based logger with file rotation and specialized methods for
 * synchronization operations, conflicts, corruption, and errors.
 *
 * Log entries are formatted as: [YYYY-MM-DD HH:MM:SS] [LEVEL] message
 *
 * When the log file exceeds rotation_size bytes it is renamed to
 * <stem>.<timestamp><ext> and a new file is opened.
 *
 * Thread-safe via internal mutex.
 */
class Logger {
public:
    using LogLevel = Config::LogLevel;

    Logger(const fs::path& log_path, LogLevel level, size_t rotation_size);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void set_level(LogLevel level);
    LogLevel get_level() const;

    void error(const std::string& message);
    void warning(const std::string& message);
    void info(const std::string& message);
    void debug(const std::string& message);

    /**
     * Log a synchronization operation (copy, replace, delete, rename).
     */
    void log_sync_operation(const std::string& operation,
                            const std::string& path,
                            const std::string& source_drive,
                            const std::string& target_drive);

    /**
     * Log a detected conflict with all involved drives.
     */
    void log_conflict(const ConflictInfo& conflict);

    /**
     * Log a quorum-based conflict resolution with the winning hash and voters.
     */
    void log_quorum_resolution(const std::string& path,
                               const std::string& winning_hash,
                               const std::vector<std::string>& voting_drives);

    /**
     * Log a data integrity failure (hash mismatch).
     */
    void log_corruption(const std::string& path,
                        const std::string& expected_hash,
                        const std::string& actual_hash);

    /**
     * Log an error with a context label.
     */
    void log_error(const std::string& context, const std::string& message);

    const fs::path& log_path() const;
    size_t rotation_count() const;

private:
    void write(LogLevel level, const std::string& message);
    void rotate_if_needed();
    void open_log_file();

    static std::string level_to_string(LogLevel level);
    static std::string current_timestamp();

    fs::path log_path_;
    LogLevel level_;
    size_t rotation_size_;
    std::ofstream file_;
    size_t rotation_count_ = 0;
    mutable std::mutex mutex_;
};

}  // namespace caravault

#endif  // CARAVAULT_LOGGER_HPP
