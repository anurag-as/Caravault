#ifndef CARAVAULT_MANIFEST_STORE_HPP
#define CARAVAULT_MANIFEST_STORE_HPP

#include "version_vector.hpp"
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <sqlite3.h>

namespace caravault {

namespace fs = std::filesystem;

/**
 * File metadata stored in the manifest database.
 */
struct FileMetadata {
    std::string path;              // Relative path from drive root
    std::string hash;              // SHA-256 hash of contents
    uint64_t size;                 // File size in bytes
    uint64_t mtime;                // Modification time (Unix timestamp)
    VersionVector version_vector;  // Causality tracking
    bool tombstone = false;        // True if file deleted
    std::optional<uint64_t> inode; // Inode number (for rename detection)

    bool operator==(const FileMetadata& other) const;
};

/**
 * A pending operation recorded in the transaction log.
 */
struct PendingOperation {
    uint64_t id;
    std::string operation;
    std::string path;
    uint64_t timestamp;
};

/**
 * ManifestStore provides persistent storage for all synchronization metadata
 * using a SQLite database stored on each drive at .caravault/manifest.db.
 *
 * All write operations use SQLite transactions for atomicity.
 * Foreign key constraints ensure referential integrity.
 */
class ManifestStore {
public:
    ManifestStore() = default;
    ~ManifestStore();

    // Non-copyable, movable
    ManifestStore(const ManifestStore&) = delete;
    ManifestStore& operator=(const ManifestStore&) = delete;
    ManifestStore(ManifestStore&& other) noexcept;
    ManifestStore& operator=(ManifestStore&& other) noexcept;

    /**
     * Open (or create) the manifest database at the given path.
     * Creates the schema if the database is new.
     *
     * @param db_path Path to the SQLite database file
     * @return Opened ManifestStore
     * @throws std::runtime_error on failure
     */
    static ManifestStore open(const fs::path& db_path);

    /**
     * Close the database connection.
     */
    void close();

    void begin_transaction();
    void commit();
    void rollback();

    void register_drive(const std::string& drive_id);
    std::vector<std::string> get_all_drives();
    void update_last_seen(const std::string& drive_id);

    void upsert_file(const FileMetadata& metadata);
    std::optional<FileMetadata> get_file(const std::string& path);
    std::vector<FileMetadata> get_all_files();
    void delete_file(const std::string& path);

    /**
     * Insert or replace a Merkle node hash.
     * @param level  0 = leaf (file), >0 = directory depth
     */
    void upsert_merkle_node(const std::string& path, const std::string& hash, int level);
    std::optional<std::string> get_merkle_hash(const std::string& path, int level);

    /**
     * Record the start of a file operation in the transaction log.
     * @return Row ID of the new log entry (pass to complete_operation)
     */
    uint64_t begin_operation(const std::string& operation, const std::string& path);
    void complete_operation(uint64_t id);
    std::vector<PendingOperation> get_incomplete_operations();

private:
    sqlite3* db_ = nullptr;

    void create_schema();
    void exec(const std::string& sql);
    [[noreturn]] void throw_error(const std::string& context) const;
};

} // namespace caravault

#endif // CARAVAULT_MANIFEST_STORE_HPP
