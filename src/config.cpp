#include "config.hpp"

#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>

namespace caravault {

namespace {

std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos)
        return {};
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> result;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, ',')) {
        auto t = trim(token);
        if (!t.empty())
            result.push_back(t);
    }
    return result;
}

std::string join_csv(const std::vector<std::string>& v) {
    std::string result;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i > 0)
            result += ',';
        result += v[i];
    }
    return result;
}

bool iequal(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// Parse a positive integer from a string. Returns -1 on failure.
long long parse_positive(const std::string& value) {
    try {
        long long v = std::stoll(value);
        return v > 0 ? v : -1;
    } catch (...) {
        return -1;
    }
}

}  // namespace

std::string Config::log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::ERROR:
            return "ERROR";
        case LogLevel::WARNING:
            return "WARNING";
        case LogLevel::INFO:
            return "INFO";
        case LogLevel::DEBUG:
            return "DEBUG";
    }
    return "INFO";
}

std::optional<Config::LogLevel> Config::log_level_from_string(const std::string& s) {
    if (iequal(s, "ERROR"))
        return LogLevel::ERROR;
    if (iequal(s, "WARNING") || iequal(s, "WARN"))
        return LogLevel::WARNING;
    if (iequal(s, "INFO"))
        return LogLevel::INFO;
    if (iequal(s, "DEBUG"))
        return LogLevel::DEBUG;
    return std::nullopt;
}

void Config::load_from_file(const fs::path& config_path) {
    std::ifstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "[caravault] WARNING: cannot open config '" << config_path.string()
                  << "', using defaults\n";
        return;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        ++line_num;
        auto comment_pos = line.find('#');
        if (comment_pos != std::string::npos)
            line = line.substr(0, comment_pos);
        line = trim(line);
        if (line.empty())
            continue;

        auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            std::cerr << "[caravault] WARNING: malformed config line " << line_num << ": '" << line
                      << "'\n";
            continue;
        }

        const std::string key = trim(line.substr(0, eq_pos));
        const std::string value = trim(line.substr(eq_pos + 1));

        if (key == "manifest_db_name") {
            if (!value.empty())
                manifest_db_name = value;
            else
                std::cerr << "[caravault] WARNING: empty 'manifest_db_name', using default\n";
        } else if (key == "log_file_path") {
            if (!value.empty())
                log_file_path = value;
            else
                std::cerr << "[caravault] WARNING: empty 'log_file_path', using default\n";
        } else if (key == "exclude_patterns") {
            exclude_patterns = split_csv(value);
        } else if (key == "quorum_threshold") {
            try {
                double v = std::stod(value);
                if (v > 0.0 && v < 1.0)
                    quorum_threshold = v;
                else
                    std::cerr << "[caravault] WARNING: 'quorum_threshold' must be in (0,1), using "
                                 "default\n";
            } catch (...) {
                std::cerr << "[caravault] WARNING: invalid 'quorum_threshold' '" << value
                          << "', using default\n";
            }
        } else if (key == "hash_algorithm") {
            if (!value.empty())
                hash_algorithm = value;
            else
                std::cerr << "[caravault] WARNING: empty 'hash_algorithm', using default\n";
        } else if (key == "cdc_chunk_size") {
            long long v = parse_positive(value);
            if (v > 0)
                cdc_chunk_size = static_cast<size_t>(v);
            else
                std::cerr
                    << "[caravault] WARNING: 'cdc_chunk_size' must be positive, using default\n";
        } else if (key == "large_file_threshold") {
            long long v = parse_positive(value);
            if (v > 0)
                large_file_threshold = static_cast<size_t>(v);
            else
                std::cerr << "[caravault] WARNING: 'large_file_threshold' must be positive, using "
                             "default\n";
        } else if (key == "log_level") {
            auto level = log_level_from_string(value);
            if (level)
                log_level = *level;
            else
                std::cerr << "[caravault] WARNING: unknown 'log_level' '" << value
                          << "', using default\n";
        } else if (key == "log_rotation_size") {
            long long v = parse_positive(value);
            if (v > 0)
                log_rotation_size = static_cast<size_t>(v);
            else
                std::cerr
                    << "[caravault] WARNING: 'log_rotation_size' must be positive, using default\n";
        } else {
            std::cerr << "[caravault] WARNING: unknown config key '" << key << "' on line "
                      << line_num << "\n";
        }
    }

    validate();
}

void Config::save_to_file(const fs::path& config_path) const {
    if (config_path.has_parent_path())
        fs::create_directories(config_path.parent_path());

    std::ofstream file(config_path);
    if (!file.is_open()) {
        std::cerr << "[caravault] WARNING: cannot write config '" << config_path.string() << "'\n";
        return;
    }

    file << "# Caravault configuration\n\n";
    file << "manifest_db_name=" << manifest_db_name.string() << "\n";
    file << "log_file_path=" << log_file_path.string() << "\n";
    file << "exclude_patterns=" << join_csv(exclude_patterns) << "\n";
    file << "quorum_threshold=" << quorum_threshold << "\n";
    file << "hash_algorithm=" << hash_algorithm << "\n";
    file << "cdc_chunk_size=" << cdc_chunk_size << "\n";
    file << "large_file_threshold=" << large_file_threshold << "\n";
    file << "log_level=" << log_level_to_string(log_level) << "\n";
    file << "log_rotation_size=" << log_rotation_size << "\n";
}

bool Config::validate() {
    bool valid = true;

    if (manifest_db_name.empty()) {
        std::cerr << "[caravault] WARNING: 'manifest_db_name' is empty, resetting to default\n";
        manifest_db_name = ".caravault/manifest.db";
        valid = false;
    }
    if (log_file_path.empty()) {
        std::cerr << "[caravault] WARNING: 'log_file_path' is empty, resetting to default\n";
        log_file_path = ".caravault/caravault.log";
        valid = false;
    }
    if (quorum_threshold <= 0.0 || quorum_threshold >= 1.0) {
        std::cerr << "[caravault] WARNING: 'quorum_threshold' out of range, resetting to default\n";
        quorum_threshold = 0.5;
        valid = false;
    }
    if (hash_algorithm.empty()) {
        std::cerr << "[caravault] WARNING: 'hash_algorithm' is empty, resetting to default\n";
        hash_algorithm = "SHA256";
        valid = false;
    }
    if (cdc_chunk_size == 0) {
        std::cerr << "[caravault] WARNING: 'cdc_chunk_size' is zero, resetting to default\n";
        cdc_chunk_size = 64 * 1024;
        valid = false;
    }
    if (large_file_threshold == 0) {
        std::cerr << "[caravault] WARNING: 'large_file_threshold' is zero, resetting to default\n";
        large_file_threshold = 1024 * 1024;
        valid = false;
    }
    if (log_rotation_size == 0) {
        std::cerr << "[caravault] WARNING: 'log_rotation_size' is zero, resetting to default\n";
        log_rotation_size = 10 * 1024 * 1024;
        valid = false;
    }

    return valid;
}

}  // namespace caravault
