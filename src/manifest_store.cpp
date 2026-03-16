#include "manifest_store.hpp"

#include <chrono>
#include <stdexcept>
#include <string>

namespace caravault {

namespace {
int64_t current_unix_time() {
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count());
}
}  // namespace

bool FileMetadata::operator==(const FileMetadata& other) const {
    return path == other.path && hash == other.hash && size == other.size && mtime == other.mtime &&
           version_vector == other.version_vector && tombstone == other.tombstone &&
           inode == other.inode;
}

ManifestStore::~ManifestStore() {
    close();
}

ManifestStore::ManifestStore(ManifestStore&& other) noexcept : db_(other.db_) {
    other.db_ = nullptr;
}

ManifestStore& ManifestStore::operator=(ManifestStore&& other) noexcept {
    if (this != &other) {
        close();
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

ManifestStore ManifestStore::open(const fs::path& db_path) {
    if (db_path.has_parent_path()) {
        fs::create_directories(db_path.parent_path());
    }

    ManifestStore store;
    int rc = sqlite3_open(db_path.string().c_str(), &store.db_);
    if (rc != SQLITE_OK) {
        std::string msg = "Failed to open database '";
        msg += db_path.string();
        msg += "': ";
        msg += sqlite3_errmsg(store.db_);
        sqlite3_close(store.db_);
        store.db_ = nullptr;
        throw std::runtime_error(msg);
    }

    store.exec("PRAGMA journal_mode=WAL");  // WAL mode: better concurrency and crash safety
    store.exec("PRAGMA foreign_keys=ON");

    store.create_schema();
    return store;
}

void ManifestStore::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void ManifestStore::create_schema() {
    exec(R"sql(
        CREATE TABLE IF NOT EXISTS drives (
            drive_id  TEXT PRIMARY KEY,
            last_seen INTEGER NOT NULL
        )
    )sql");

    exec(R"sql(
        CREATE TABLE IF NOT EXISTS files (
            path           TEXT    PRIMARY KEY,
            hash           TEXT    NOT NULL,
            size           INTEGER NOT NULL,
            mtime          INTEGER NOT NULL,
            version_vector TEXT    NOT NULL,
            tombstone      INTEGER NOT NULL DEFAULT 0,
            inode          INTEGER
        )
    )sql");

    exec("CREATE INDEX IF NOT EXISTS idx_files_hash ON files(hash)");

    exec(R"sql(
        CREATE TABLE IF NOT EXISTS merkle_nodes (
            path  TEXT    NOT NULL,
            level INTEGER NOT NULL,
            hash  TEXT    NOT NULL,
            PRIMARY KEY (path, level)
        )
    )sql");

    exec(R"sql(
        CREATE TABLE IF NOT EXISTS transaction_log (
            id        INTEGER PRIMARY KEY AUTOINCREMENT,
            operation TEXT    NOT NULL,
            path      TEXT    NOT NULL,
            timestamp INTEGER NOT NULL,
            completed INTEGER NOT NULL DEFAULT 0
        )
    )sql");

    exec("CREATE INDEX IF NOT EXISTS idx_txlog_completed ON transaction_log(completed)");
}

void ManifestStore::begin_transaction() {
    exec("BEGIN");
}

void ManifestStore::commit() {
    exec("COMMIT");
}

void ManifestStore::rollback() {
    exec("ROLLBACK");
}

void ManifestStore::register_drive(const std::string& drive_id) {
    const char* sql =
        "INSERT INTO drives(drive_id, last_seen) VALUES(?, ?)"
        " ON CONFLICT(drive_id) DO UPDATE SET last_seen=excluded.last_seen";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("register_drive prepare");
    }

    sqlite3_bind_text(stmt, 1, drive_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, current_unix_time());

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw_error("register_drive step");
    }
}

std::vector<std::string> ManifestStore::get_all_drives() {
    const char* sql = "SELECT drive_id FROM drives";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("get_all_drives prepare");
    }

    std::vector<std::string> drives;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        drives.emplace_back(id ? id : "");
    }
    sqlite3_finalize(stmt);
    return drives;
}

void ManifestStore::update_last_seen(const std::string& drive_id) {
    const char* sql = "UPDATE drives SET last_seen=? WHERE drive_id=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("update_last_seen prepare");
    }

    sqlite3_bind_int64(stmt, 1, current_unix_time());
    sqlite3_bind_text(stmt, 2, drive_id.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw_error("update_last_seen step");
    }
}

void ManifestStore::upsert_file(const FileMetadata& m) {
    const char* sql =
        "INSERT INTO files(path, hash, size, mtime, version_vector, tombstone, inode)"
        " VALUES(?,?,?,?,?,?,?)"
        " ON CONFLICT(path) DO UPDATE SET"
        "  hash=excluded.hash,"
        "  size=excluded.size,"
        "  mtime=excluded.mtime,"
        "  version_vector=excluded.version_vector,"
        "  tombstone=excluded.tombstone,"
        "  inode=excluded.inode";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("upsert_file prepare");
    }

    std::string vv_json = m.version_vector.to_json();

    sqlite3_bind_text(stmt, 1, m.path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, m.hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<int64_t>(m.size));
    sqlite3_bind_int64(stmt, 4, static_cast<int64_t>(m.mtime));
    sqlite3_bind_text(stmt, 5, vv_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, m.tombstone ? 1 : 0);
    if (m.inode.has_value()) {
        sqlite3_bind_int64(stmt, 7, static_cast<int64_t>(*m.inode));
    } else {
        sqlite3_bind_null(stmt, 7);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw_error("upsert_file step");
    }
}

static FileMetadata row_to_file_metadata(sqlite3_stmt* stmt) {
    FileMetadata m;
    auto col_text = [&](int col) -> std::string {
        const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return p ? p : "";
    };

    m.path = col_text(0);
    m.hash = col_text(1);
    m.size = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
    m.mtime = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
    m.version_vector = VersionVector::from_json(col_text(4));
    m.tombstone = sqlite3_column_int(stmt, 5) != 0;
    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
        m.inode = static_cast<uint64_t>(sqlite3_column_int64(stmt, 6));
    }
    return m;
}

std::optional<FileMetadata> ManifestStore::get_file(const std::string& path) {
    const char* sql =
        "SELECT path, hash, size, mtime, version_vector, tombstone, inode"
        " FROM files WHERE path=?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("get_file prepare");
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<FileMetadata> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = row_to_file_metadata(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<FileMetadata> ManifestStore::get_all_files() {
    const char* sql = "SELECT path, hash, size, mtime, version_vector, tombstone, inode FROM files";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("get_all_files prepare");
    }

    std::vector<FileMetadata> files;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        files.push_back(row_to_file_metadata(stmt));
    }
    sqlite3_finalize(stmt);
    return files;
}

void ManifestStore::delete_file(const std::string& path) {
    const char* sql = "DELETE FROM files WHERE path=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("delete_file prepare");
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw_error("delete_file step");
    }
}

void ManifestStore::upsert_merkle_node(const std::string& path,
                                       const std::string& hash,
                                       int level) {
    const char* sql =
        "INSERT INTO merkle_nodes(path, level, hash) VALUES(?,?,?)"
        " ON CONFLICT(path, level) DO UPDATE SET hash=excluded.hash";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("upsert_merkle_node prepare");
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, level);
    sqlite3_bind_text(stmt, 3, hash.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw_error("upsert_merkle_node step");
    }
}

std::optional<std::string> ManifestStore::get_merkle_hash(const std::string& path, int level) {
    const char* sql = "SELECT hash FROM merkle_nodes WHERE path=? AND level=?";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("get_merkle_hash prepare");
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, level);

    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* h = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        result = h ? h : "";
    }
    sqlite3_finalize(stmt);
    return result;
}

uint64_t ManifestStore::begin_operation(const std::string& operation, const std::string& path) {
    const char* sql =
        "INSERT INTO transaction_log(operation, path, timestamp, completed)"
        " VALUES(?,?,?,0)";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("begin_operation prepare");
    }

    sqlite3_bind_text(stmt, 1, operation.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, current_unix_time());

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw_error("begin_operation step");
    }

    return static_cast<uint64_t>(sqlite3_last_insert_rowid(db_));
}

void ManifestStore::complete_operation(uint64_t id) {
    const char* sql = "UPDATE transaction_log SET completed=1 WHERE id=?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("complete_operation prepare");
    }

    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(id));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw_error("complete_operation step");
    }
}

std::vector<PendingOperation> ManifestStore::get_incomplete_operations() {
    const char* sql =
        "SELECT id, operation, path, timestamp"
        " FROM transaction_log WHERE completed=0 ORDER BY id";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        throw_error("get_incomplete_operations prepare");
    }

    std::vector<PendingOperation> ops;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PendingOperation op;
        op.id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        auto col_text = [&](int col) -> std::string {
            const char* p = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return p ? p : "";
        };
        op.operation = col_text(1);
        op.path = col_text(2);
        op.timestamp = static_cast<uint64_t>(sqlite3_column_int64(stmt, 3));
        ops.push_back(std::move(op));
    }
    sqlite3_finalize(stmt);
    return ops;
}

void ManifestStore::exec(const std::string& sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = "SQL error: ";
        if (errmsg) {
            msg += errmsg;
            sqlite3_free(errmsg);
        }
        throw std::runtime_error(msg);
    }
}

void ManifestStore::throw_error(const std::string& context) const {
    std::string msg = context + ": " + sqlite3_errmsg(db_);
    throw std::runtime_error(msg);
}

}  // namespace caravault
