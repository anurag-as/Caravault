#ifndef CARAVAULT_CONFIG_HPP
#define CARAVAULT_CONFIG_HPP

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace caravault {

/**
 * Configuration settings for the Caravault synchronization system.
 *
 * Loaded from a key=value file. Missing or invalid values fall back to
 * defaults. All values are validated on load.
 */
struct Config {
    enum class LogLevel { ERROR, WARNING, INFO, DEBUG };

    fs::path manifest_db_name = ".caravault/manifest.db";
    fs::path log_file_path = ".caravault/caravault.log";

    std::vector<std::string> exclude_patterns = {".git", ".DS_Store", "*.tmp"};
    double quorum_threshold = 0.5;
    std::string hash_algorithm = "SHA256";

    size_t cdc_chunk_size = 64 * 1024;
    size_t large_file_threshold = 1024 * 1024;

    LogLevel log_level = LogLevel::INFO;
    size_t log_rotation_size = 10 * 1024 * 1024;

    /**
     * Load settings from a key=value file. Missing keys keep their defaults.
     * Invalid values are rejected and defaults are kept; a warning is printed.
     */
    void load_from_file(const fs::path& config_path);

    /**
     * Persist the current settings to a key=value file.
     * Creates parent directories if they do not exist.
     */
    void save_to_file(const fs::path& config_path) const;

    /**
     * Validate all settings. Out-of-range values are reset to defaults and a
     * warning is printed.
     *
     * @return true if all values were already valid, false if any were reset.
     */
    bool validate();

    static std::string log_level_to_string(LogLevel level);

    /**
     * @return std::nullopt if the string does not match a known level.
     */
    static std::optional<LogLevel> log_level_from_string(const std::string& s);
};

}  // namespace caravault

#endif  // CARAVAULT_CONFIG_HPP
