#ifndef CARAVAULT_EXECUTOR_HPP
#define CARAVAULT_EXECUTOR_HPP

#include "manifest_store.hpp"
#include "sync_planner.hpp"

#include <map>
#include <string>

namespace caravault {

/**
 * Executor performs synchronization operations atomically with crash recovery.
 *
 * All file writes use the temp-then-rename pattern:
 *   1. Log operation to transaction_log before touching the filesystem
 *   2. Write to {target}.caravault.tmp
 *   3. Verify SHA-256 hash of the written temp file
 *   4. Atomically rename temp file to target path
 *   5. Update manifest and mark log entry complete
 *
 * On startup, recover_incomplete_operations() processes any leftover
 * transaction log entries from a previous crash.
 */
class Executor {
public:
    struct ExecutionResult {
        bool success = false;
        std::string error_message;
        size_t bytes_transferred = 0;
    };

    /**
     * Execute a single SyncOp.
     * drive_roots maps drive_id -> absolute mount-point path on the host.
     * manifests   maps drive_id -> ManifestStore* for that drive.
     * Never throws; errors are captured in ExecutionResult.
     */
    ExecutionResult execute(const SyncOp& op,
                            const std::map<std::string, fs::path>& drive_roots,
                            std::map<std::string, ManifestStore*>& manifests);

    /**
     * Process incomplete entries in the transaction log on startup.
     * For each incomplete WRITE entry:
     *  - Temp file exists and hash is valid: complete the rename.
     *  - Temp file exists but hash is invalid: delete it.
     *  - Temp file absent: nothing to do.
     * All entries are marked complete so the log stays clean.
     */
    void recover_incomplete_operations(ManifestStore& store, const fs::path& drive_root);

    /**
     * Return true if the SHA-256 hash of file_path matches expected_hash.
     * Returns false on mismatch or any I/O error.
     */
    static bool verify_hash(const fs::path& file_path, const std::string& expected_hash);

    /**
     * Check whether a drive root is currently accessible (not disconnected).
     * Returns false if the path does not exist or cannot be stat'd.
     */
    static bool is_drive_accessible(const fs::path& drive_root);

    /**
     * Verify rel_path's on-disk hash against its manifest entry.
     * Marks the file as corrupted in the manifest and returns false on mismatch.
     * Returns true (without touching the manifest) when no entry exists.
     */
    static bool read_file_verified(const std::string& rel_path,
                                   const fs::path& abs_path,
                                   ManifestStore& store);

    /**
     * Restore a corrupted file from an uncorrupted replica on another drive.
     * Iterates drive_roots until a drive with a matching, intact copy is found,
     * then atomically copies it to target_drive.
     * Returns true on success, false if all replicas are corrupted or absent.
     */
    bool restore_from_replica(const std::string& rel_path,
                              const std::string& target_drive,
                              const std::map<std::string, fs::path>& drive_roots,
                              std::map<std::string, ManifestStore*>& manifests);

private:
    /**
     * Write source_path to target_path atomically:
     * log → write tmp → verify → rename → update manifest → complete log.
     * Throws std::runtime_error on failure; always leaves the log entry complete.
     */
    void atomic_write(const fs::path& source_path,
                      const fs::path& target_path,
                      const std::string& expected_hash,
                      const FileMetadata& new_meta,
                      ManifestStore& target_store);

    /** Copy permissions and mtime from source to target (best-effort, no-throw). */
    static void copy_metadata(const fs::path& source, const fs::path& target);
};

}  // namespace caravault

#endif  // CARAVAULT_EXECUTOR_HPP
