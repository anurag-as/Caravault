#ifndef CARAVAULT_PROGRESS_REPORTER_HPP
#define CARAVAULT_PROGRESS_REPORTER_HPP

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>

namespace caravault {

// Reports synchronization progress to stderr with an in-place progress bar.
//
// When stderr is a TTY, uses ANSI carriage-return to update the bar in-place:
//   [=====>    ] 45% (450/1000 files) - Copying foo.txt
//
// When stderr is not a TTY (e.g. redirected to a file), falls back to
// simple line-by-line output so logs remain readable.
//
// Thread-safe via internal mutex.
class ProgressReporter {
public:
    struct SummaryStats {
        size_t files_copied = 0;
        size_t files_deleted = 0;
        size_t files_renamed = 0;
        size_t conflicts_resolved = 0;
        size_t bytes_transferred = 0;
        double duration_seconds = 0.0;
    };

    ProgressReporter();
    ~ProgressReporter() = default;

    ProgressReporter(const ProgressReporter&) = delete;
    ProgressReporter& operator=(const ProgressReporter&) = delete;

    // Begin a sync session. Records start time and sets totals for percentages.
    void start(size_t total_files, size_t total_bytes);

    // Update files processed and current operation label. Redraws the bar.
    void update(size_t files_processed, const std::string& current_op);

    // Update bytes transferred (for large-file progress). Redraws the bar.
    void update_bytes(size_t bytes_transferred);

    // Clear the progress line and store stats for display_summary().
    void finish(const SummaryStats& stats);

    // Print a human-readable summary of the completed sync session to stderr.
    void display_summary() const;

    // Accessors (primarily for testing).
    size_t files_processed() const;
    size_t total_files() const;
    size_t bytes_transferred() const;
    size_t total_bytes() const;
    std::string current_operation() const;

private:
    void render();
    static std::string format_bytes(size_t bytes);

    mutable std::mutex mutex_;

    size_t files_processed_ = 0;
    size_t total_files_ = 0;
    size_t bytes_transferred_ = 0;
    size_t total_bytes_ = 0;
    std::string current_operation_;

    bool is_tty_ = false;
    bool started_ = false;

    std::chrono::steady_clock::time_point start_time_;
    SummaryStats summary_;
};

}  // namespace caravault

#endif  // CARAVAULT_PROGRESS_REPORTER_HPP
