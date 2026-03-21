#include "progress_reporter.hpp"

#include <cstdio>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#define ISATTY(fd) _isatty(fd)
#define STDERR_FILENO 2
#else
#include <unistd.h>
#define ISATTY(fd) isatty(fd)
#endif

namespace caravault {

namespace {
constexpr int kBarWidth = 20;
constexpr size_t kMaxOpLen = 40;
}  // namespace

ProgressReporter::ProgressReporter() : is_tty_(ISATTY(STDERR_FILENO) != 0) {}

void ProgressReporter::start(size_t total_files, size_t total_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    total_files_ = total_files;
    total_bytes_ = total_bytes;
    files_processed_ = 0;
    bytes_transferred_ = 0;
    current_operation_.clear();
    summary_ = {};
    started_ = true;
    start_time_ = std::chrono::steady_clock::now();
}

void ProgressReporter::update(size_t files_processed, const std::string& current_op) {
    std::lock_guard<std::mutex> lock(mutex_);
    files_processed_ = files_processed;
    current_operation_ = current_op;
    render();
}

void ProgressReporter::update_bytes(size_t bytes_transferred) {
    std::lock_guard<std::mutex> lock(mutex_);
    bytes_transferred_ = bytes_transferred;
    render();
}

void ProgressReporter::finish(const SummaryStats& stats) {
    std::lock_guard<std::mutex> lock(mutex_);
    summary_ = stats;
    if (is_tty_) {
#ifdef _WIN32
        std::fprintf(stderr, "\r%-80s\r", "");
#else
        std::fprintf(stderr, "\r\033[K");
#endif
    } else {
        std::fprintf(stderr, "\n");
    }
    std::fflush(stderr);
    started_ = false;
}

void ProgressReporter::display_summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::fprintf(stderr, "\nSync complete:\n");
    std::fprintf(stderr, "  Files copied:       %zu\n", summary_.files_copied);
    std::fprintf(stderr, "  Files deleted:      %zu\n", summary_.files_deleted);
    std::fprintf(stderr, "  Files renamed:      %zu\n", summary_.files_renamed);
    std::fprintf(stderr, "  Conflicts resolved: %zu\n", summary_.conflicts_resolved);
    std::fprintf(
        stderr, "  Bytes transferred:  %s\n", format_bytes(summary_.bytes_transferred).c_str());
    std::fprintf(stderr, "  Duration:           %.2f s\n", summary_.duration_seconds);
    std::fflush(stderr);
}

size_t ProgressReporter::files_processed() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return files_processed_;
}

size_t ProgressReporter::total_files() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_files_;
}

size_t ProgressReporter::bytes_transferred() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bytes_transferred_;
}

size_t ProgressReporter::total_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return total_bytes_;
}

std::string ProgressReporter::current_operation() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_operation_;
}

void ProgressReporter::render() {
    int pct = total_files_ > 0 ? static_cast<int>((files_processed_ * 100) / total_files_) : 0;
    int filled = (pct * kBarWidth) / 100;

    std::string bar(static_cast<size_t>(kBarWidth), ' ');
    for (int i = 0; i < filled; ++i)
        bar[static_cast<size_t>(i)] = '=';
    if (filled < kBarWidth)
        bar[static_cast<size_t>(filled)] = '>';

    std::string op = current_operation_;
    if (op.size() > kMaxOpLen)
        op = "..." + op.substr(op.size() - (kMaxOpLen - 3));

    if (is_tty_) {
        std::fprintf(stderr,
                     "\r[%s] %3d%% (%zu/%zu files) - %s",
                     bar.c_str(),
                     pct,
                     files_processed_,
                     total_files_,
                     op.c_str());
    } else {
        std::fprintf(stderr,
                     "[%s] %3d%% (%zu/%zu files) - %s\n",
                     bar.c_str(),
                     pct,
                     files_processed_,
                     total_files_,
                     op.c_str());
    }
    std::fflush(stderr);
}

std::string ProgressReporter::format_bytes(size_t bytes) {
    std::ostringstream oss;
    if (bytes < 1024) {
        oss << bytes << " B";
    } else if (bytes < 1024 * 1024) {
        oss << std::fixed << std::setprecision(1) << (static_cast<double>(bytes) / 1024.0) << " KB";
    } else if (bytes < 1024ULL * 1024 * 1024) {
        oss << std::fixed << std::setprecision(1)
            << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    } else {
        oss << std::fixed << std::setprecision(2)
            << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << " GB";
    }
    return oss.str();
}

}  // namespace caravault
