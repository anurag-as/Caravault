#include "executor.hpp"

#include "merkle_engine.hpp"
#include "progress_reporter.hpp"

#include <array>
#include <fstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <utime.h>
#endif

namespace caravault {

namespace {

fs::path tmp_path(const fs::path& target) {
    return fs::path(target.string() + ".caravault.tmp");
}

size_t copy_file_contents(const fs::path& src, const fs::path& dst) {
    std::ifstream in(src, std::ios::binary);
    if (!in) {
        std::error_code ec;
        if (!fs::exists(src, ec))
            throw std::runtime_error("Source file not found: " + src.string());
        throw std::runtime_error("Cannot read source file (check permissions): " + src.string());
    }

    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("Cannot open destination for writing (check permissions/space): " +
                                 dst.string());

    std::array<char, 64 * 1024> buf{};
    size_t total = 0;
    while (in.read(buf.data(), buf.size()) || in.gcount() > 0) {
        auto n = static_cast<size_t>(in.gcount());
        out.write(buf.data(), static_cast<std::streamsize>(n));
        if (!out)
            throw std::runtime_error("Write failed (disk full or I/O error): " + dst.string());
        total += n;
    }
    out.flush();
    if (!out)
        throw std::runtime_error("Flush failed (disk full or I/O error): " + dst.string());
    return total;
}

constexpr size_t kLargeFileThreshold = 1024 * 1024;  // 1 MB
constexpr size_t kCopyChunkSize = 64 * 1024;          // 64 KB

// Copy src -> dst in 64 KB chunks, calling reporter->update_bytes() after each chunk.
// Returns total bytes written.
size_t copy_file_with_progress(const fs::path& src,
                               const fs::path& dst,
                               ProgressReporter* reporter,
                               size_t initial_bytes_transferred) {
    std::ifstream in(src, std::ios::binary);
    if (!in) {
        std::error_code ec;
        if (!fs::exists(src, ec))
            throw std::runtime_error("Source file not found: " + src.string());
        throw std::runtime_error("Cannot read source file (check permissions): " + src.string());
    }

    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("Cannot open destination for writing (check permissions/space): " +
                                 dst.string());

    std::array<char, kCopyChunkSize> buf{};
    size_t total = 0;
    size_t running_bytes = initial_bytes_transferred;
    while (in.read(buf.data(), kCopyChunkSize) || in.gcount() > 0) {
        auto n = static_cast<size_t>(in.gcount());
        out.write(buf.data(), static_cast<std::streamsize>(n));
        if (!out)
            throw std::runtime_error("Write failed (disk full or I/O error): " + dst.string());
        total += n;
        running_bytes += n;
        if (reporter)
            reporter->update_bytes(running_bytes);
    }
    out.flush();
    if (!out)
        throw std::runtime_error("Flush failed (disk full or I/O error): " + dst.string());
    return total;
}

}  // namespace

bool Executor::verify_hash(const fs::path& file_path, const std::string& expected_hash) {
    try {
        return MerkleEngine::compute_file_hash(file_path) == expected_hash;
    } catch (...) {
        return false;
    }
}

bool Executor::is_drive_accessible(const fs::path& drive_root) {
    std::error_code ec;
    return fs::exists(drive_root, ec) && !ec;
}

bool Executor::read_file_verified(const std::string& rel_path,
                                  const fs::path& abs_path,
                                  ManifestStore& store) {
    auto meta = store.get_file(rel_path);
    if (!meta.has_value())
        return true;

    if (verify_hash(abs_path, meta->hash))
        return true;

    meta->corrupted = true;
    store.upsert_file(*meta);
    return false;
}

bool Executor::restore_from_replica(const std::string& rel_path,
                                    const std::string& target_drive,
                                    const std::map<std::string, fs::path>& drive_roots,
                                    std::map<std::string, ManifestStore*>& manifests) {
    auto target_root_it = drive_roots.find(target_drive);
    if (target_root_it == drive_roots.end())
        return false;

    ManifestStore* target_store = manifests.at(target_drive);
    auto target_meta = target_store->get_file(rel_path);
    if (!target_meta.has_value())
        return false;

    const std::string& expected_hash = target_meta->hash;

    for (auto& [drive_id, root] : drive_roots) {
        if (drive_id == target_drive)
            continue;

        auto it = manifests.find(drive_id);
        if (it == manifests.end())
            continue;

        auto src_meta = it->second->get_file(rel_path);
        if (!src_meta.has_value() || src_meta->tombstone || src_meta->hash != expected_hash)
            continue;

        fs::path src_abs = root / rel_path;
        if (!fs::exists(src_abs) || !verify_hash(src_abs, expected_hash))
            continue;

        try {
            FileMetadata restored = *target_meta;
            restored.corrupted = false;
            atomic_write(
                src_abs, target_root_it->second / rel_path, expected_hash, restored, *target_store);
            return true;
        } catch (...) {
            // Try next replica.
        }
    }

    return false;
}

void Executor::copy_metadata(const fs::path& source, const fs::path& target) {
#ifdef _WIN32
    HANDLE hsrc = CreateFileW(source.wstring().c_str(),
                              GENERIC_READ,
                              FILE_SHARE_READ,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (hsrc == INVALID_HANDLE_VALUE)
        return;

    FILETIME mtime{};
    GetFileTime(hsrc, nullptr, nullptr, &mtime);
    CloseHandle(hsrc);

    HANDLE hdst = CreateFileW(target.wstring().c_str(),
                              GENERIC_WRITE,
                              0,
                              nullptr,
                              OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (hdst == INVALID_HANDLE_VALUE)
        return;
    SetFileTime(hdst, nullptr, nullptr, &mtime);
    CloseHandle(hdst);
#else
    struct stat st = {};
    if (::stat(source.c_str(), &st) != 0)
        return;
    ::chmod(target.c_str(), st.st_mode & 07777);
    struct utimbuf times = {};
    times.actime = st.st_mtime;
    times.modtime = st.st_mtime;
    ::utime(target.c_str(), &times);
#endif
}

void Executor::atomic_write(const fs::path& source_path,
                            const fs::path& target_path,
                            const std::string& expected_hash,
                            const FileMetadata& new_meta,
                            ManifestStore& target_store,
                            ProgressReporter* reporter,
                            size_t initial_bytes_transferred) {
    if (!verify_hash(source_path, expected_hash))
        throw std::runtime_error("Source hash mismatch before transfer: " + source_path.string());

    if (target_path.has_parent_path())
        fs::create_directories(target_path.parent_path());

    fs::path tmp = tmp_path(target_path);

    uint64_t log_id = target_store.begin_operation("WRITE", target_path.string());

    try {
        std::error_code size_ec;
        auto file_size = fs::file_size(source_path, size_ec);
        bool is_large = !size_ec && file_size > kLargeFileThreshold;

        if (is_large && reporter) {
            copy_file_with_progress(source_path, tmp, reporter, initial_bytes_transferred);
        } else {
            copy_file_contents(source_path, tmp);
        }
        copy_metadata(source_path, tmp);

        if (!verify_hash(tmp, expected_hash)) {
            fs::remove(tmp);
            throw std::runtime_error("Hash mismatch after write to tmp: " + tmp.string());
        }

        fs::rename(tmp, target_path);
        target_store.upsert_file(new_meta);
        target_store.complete_operation(log_id);

    } catch (...) {
        std::error_code ec;
        fs::remove(tmp, ec);
        target_store.complete_operation(log_id);
        throw;
    }
}

Executor::ExecutionResult Executor::execute(const SyncOp& op,
                                            const std::map<std::string, fs::path>& drive_roots,
                                            std::map<std::string, ManifestStore*>& manifests,
                                            ProgressReporter* reporter,
                                            size_t* files_processed_counter) {
    ExecutionResult result;

    try {
        auto target_root_it = drive_roots.find(op.target_drive_id);
        if (target_root_it == drive_roots.end()) {
            result.error_message = "Unknown target drive: " + op.target_drive_id;
            return result;
        }
        const fs::path& target_root = target_root_it->second;

        std::error_code ec;
        if (!fs::exists(target_root, ec) || ec) {
            result.error_message =
                "Target drive disconnected or inaccessible: " + target_root.string();
            return result;
        }

        ManifestStore* target_store = manifests.at(op.target_drive_id);

        switch (op.type) {
            case SyncOpType::MKDIR: {
                fs::create_directories(target_root / op.path, ec);
                if (ec) {
                    result.error_message = "Cannot create directory " +
                                           (target_root / op.path).string() + ": " + ec.message();
                    return result;
                }
                result.success = true;
                break;
            }

            case SyncOpType::COPY:
            case SyncOpType::REPLACE: {
                auto src_root_it = drive_roots.find(op.source_drive_id);
                if (src_root_it == drive_roots.end()) {
                    result.error_message = "Unknown source drive: " + op.source_drive_id;
                    return result;
                }

                if (!fs::exists(src_root_it->second, ec) || ec) {
                    result.error_message = "Source drive disconnected or inaccessible: " +
                                           src_root_it->second.string();
                    return result;
                }

                ManifestStore* src_store = manifests.at(op.source_drive_id);

                auto src_meta = src_store->get_file(op.path);
                if (!src_meta.has_value()) {
                    result.error_message = "Source file not in manifest: " + op.path;
                    return result;
                }

                FileMetadata new_meta = *src_meta;
                new_meta.version_vector = op.new_version_vector;

                // Determine bytes already transferred (for cumulative progress).
                size_t bytes_before = reporter ? reporter->bytes_transferred() : 0;

                atomic_write(src_root_it->second / op.path,
                             target_root / op.path,
                             src_meta->hash,
                             new_meta,
                             *target_store,
                             reporter,
                             bytes_before);

                result.bytes_transferred = src_meta->size;
                result.success = true;
                break;
            }

            case SyncOpType::DELETE: {
                fs::path target_path = target_root / op.path;
                uint64_t log_id = target_store->begin_operation("DELETE", target_path.string());

                fs::remove(target_path, ec);

                auto existing = target_store->get_file(op.path);
                FileMetadata tombstone_meta;
                if (existing.has_value()) {
                    tombstone_meta = *existing;
                } else {
                    tombstone_meta.path = op.path;
                }
                tombstone_meta.tombstone = true;
                tombstone_meta.version_vector = op.new_version_vector;
                target_store->upsert_file(tombstone_meta);

                target_store->complete_operation(log_id);
                result.success = true;
                break;
            }

            case SyncOpType::RENAME: {
                if (!op.new_path.has_value()) {
                    result.error_message = "RENAME op missing new_path for: " + op.path;
                    return result;
                }

                fs::path old_path = target_root / op.path;
                fs::path new_path = target_root / *op.new_path;
                uint64_t log_id = target_store->begin_operation("RENAME", old_path.string());

                fs::create_directories(new_path.parent_path(), ec);
                if (ec) {
                    target_store->complete_operation(log_id);
                    result.error_message =
                        "Cannot create parent directory for rename target: " + ec.message();
                    return result;
                }

                fs::rename(old_path, new_path, ec);
                if (ec) {
                    target_store->complete_operation(log_id);
                    result.error_message = "Rename failed: " + old_path.string() + " -> " +
                                           new_path.string() + ": " + ec.message();
                    return result;
                }

                auto existing = target_store->get_file(op.path);
                if (existing.has_value()) {
                    target_store->delete_file(op.path);
                    FileMetadata new_meta = *existing;
                    new_meta.path = *op.new_path;
                    new_meta.version_vector = op.new_version_vector;
                    target_store->upsert_file(new_meta);
                }

                target_store->complete_operation(log_id);
                result.success = true;
                break;
            }
        }

    } catch (const std::exception& e) {
        result.success = false;
        result.error_message = e.what();
    }

    // Report per-file progress after the operation completes.
    if (result.success && reporter && files_processed_counter) {
        reporter->update(++(*files_processed_counter), op.path);
    }

    return result;
}

void Executor::recover_incomplete_operations(ManifestStore& store, const fs::path& drive_root) {
    for (const auto& op : store.get_incomplete_operations()) {
        if (op.operation == "WRITE") {
            fs::path target_path(op.path);
            fs::path tmp = tmp_path(target_path);

            if (fs::exists(tmp)) {
                std::error_code ec;
                fs::path rel = fs::relative(target_path, drive_root, ec);
                std::string rel_str = ec ? op.path : rel.generic_string();

                auto meta = store.get_file(rel_str);
                bool valid =
                    meta.has_value() && !meta->hash.empty() && verify_hash(tmp, meta->hash);

                if (valid) {
                    fs::rename(tmp, target_path, ec);
                    if (ec)
                        fs::remove(tmp, ec);
                } else {
                    fs::remove(tmp, ec);
                }
            }
        }
        store.complete_operation(op.id);
    }
}

}  // namespace caravault
