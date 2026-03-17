#include "logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace caravault {

Logger::Logger(const fs::path& log_path, LogLevel level, size_t rotation_size)
    : log_path_(log_path), level_(level), rotation_size_(rotation_size) {
    open_log_file();
}

Logger::~Logger() {
    if (file_.is_open())
        file_.close();
}

void Logger::set_level(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

Logger::LogLevel Logger::get_level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

void Logger::error(const std::string& message) {
    write(LogLevel::ERROR, message);
}

void Logger::warning(const std::string& message) {
    write(LogLevel::WARNING, message);
}

void Logger::info(const std::string& message) {
    write(LogLevel::INFO, message);
}

void Logger::debug(const std::string& message) {
    write(LogLevel::DEBUG, message);
}

void Logger::log_sync_operation(const std::string& operation,
                                const std::string& path,
                                const std::string& source_drive,
                                const std::string& target_drive) {
    write(LogLevel::INFO,
          "SYNC_OP op=" + operation + " path=" + path + " src=" + source_drive +
              " dst=" + target_drive);
}

void Logger::log_conflict(const ConflictInfo& conflict) {
    std::string msg = "CONFLICT path=" + conflict.path + " drives=[";
    bool first = true;
    for (const auto& [drive_id, _] : conflict.versions) {
        if (!first)
            msg += ",";
        msg += drive_id;
        first = false;
    }
    msg += "]";
    write(LogLevel::WARNING, msg);
}

void Logger::log_quorum_resolution(const std::string& path,
                                   const std::string& winning_hash,
                                   const std::vector<std::string>& voting_drives) {
    std::string msg =
        "QUORUM_RESOLUTION path=" + path + " winning_hash=" + winning_hash + " voters=[";
    for (size_t i = 0; i < voting_drives.size(); ++i) {
        if (i > 0)
            msg += ",";
        msg += voting_drives[i];
    }
    msg += "]";
    write(LogLevel::INFO, msg);
}

void Logger::log_corruption(const std::string& path,
                            const std::string& expected_hash,
                            const std::string& actual_hash) {
    write(LogLevel::ERROR,
          "CORRUPTION path=" + path + " expected=" + expected_hash + " actual=" + actual_hash);
}

void Logger::log_error(const std::string& context, const std::string& message) {
    write(LogLevel::ERROR, "[" + context + "] " + message);
}

const fs::path& Logger::log_path() const {
    return log_path_;
}

size_t Logger::rotation_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rotation_count_;
}

void Logger::write(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(level) > static_cast<int>(level_))
        return;
    rotate_if_needed();
    file_ << "[" << current_timestamp() << "] [" << level_to_string(level) << "] " << message
          << "\n";
    file_.flush();
}

void Logger::rotate_if_needed() {
    if (!fs::exists(log_path_))
        return;
    std::error_code ec;
    auto size = fs::file_size(log_path_, ec);
    if (ec || size < rotation_size_)
        return;

    file_.close();

    std::string ts = current_timestamp();
    for (char& c : ts)
        if (c == ':' || c == ' ')
            c = '-';

    fs::path rotated = log_path_.parent_path() /
                       (log_path_.stem().string() + "." + ts + log_path_.extension().string());
    fs::rename(log_path_, rotated, ec);

    ++rotation_count_;
    open_log_file();
}

void Logger::open_log_file() {
    if (log_path_.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(log_path_.parent_path(), ec);
    }
    file_.open(log_path_, std::ios::app);
    if (!file_.is_open())
        throw std::runtime_error("Logger: cannot open log file: " + log_path_.string());
}

std::string Logger::level_to_string(LogLevel level) {
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

std::string Logger::current_timestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto t = system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &t);
#else
    gmtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

}  // namespace caravault
