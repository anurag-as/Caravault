#ifndef CARAVAULT_PLATFORM_HPP
#define CARAVAULT_PLATFORM_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace caravault {

namespace fs = std::filesystem;

/**
 * Normalize path separators to forward slashes for portable database storage.
 * Strips trailing slashes except for the root "/".
 * Idempotent: normalizing an already-normalized path returns the same string.
 */
std::string normalize_path(const std::string& path);

/**
 * Convert a normalized (forward-slash) path back to a native filesystem path.
 * On Windows, forward slashes are converted to backslashes.
 */
fs::path to_native_path(const std::string& normalized);

/**
 * Return the current platform name: "linux", "macos", or "windows".
 */
std::string get_platform_name();

/**
 * Portable representation of POSIX-style file permissions.
 * On Windows, only the read-only attribute is reflected; all other bits
 * are approximated via std::filesystem::permissions.
 */
struct FilePermissions {
    bool owner_read = false;
    bool owner_write = false;
    bool owner_exec = false;
    bool group_read = false;
    bool group_write = false;
    bool group_exec = false;
    bool other_read = false;
    bool other_write = false;
    bool other_exec = false;
    uint32_t raw_mode = 0;  // Platform-specific raw value (octal mode on Unix)
};

/**
 * Read the permissions of the file at path.
 * Returns a zeroed FilePermissions on error.
 */
FilePermissions get_permissions(const fs::path& path);

/**
 * Apply perms to the file at path.
 * Best-effort on Windows (only read-only attribute is controllable).
 * Returns true on success.
 */
bool set_permissions(const fs::path& path, const FilePermissions& perms);

/**
 * Serialize permissions to a portable three-digit octal string, e.g. "755".
 */
std::string serialize_permissions(const FilePermissions& perms);

/**
 * Deserialize permissions from a three-digit octal string produced by
 * serialize_permissions(). Returns zeroed permissions on malformed input.
 */
FilePermissions deserialize_permissions(const std::string& perms_str);

/**
 * A mounted drive visible to the current platform.
 */
struct DriveMount {
    std::string drive_id_hint;  // Volume label or drive letter
    fs::path mount_point;       // Absolute path to the mount point
    uint64_t total_bytes = 0;
    uint64_t free_bytes = 0;
};

/**
 * Detect all mounted drives visible to the current platform.
 *   Linux:   subdirectories of /mnt and /media that are mount points
 *   macOS:   subdirectories of /Volumes that are mount points
 *   Windows: drive letters A-Z of type DRIVE_REMOVABLE or DRIVE_FIXED
 */
std::vector<DriveMount> detect_drives();

/**
 * Return true if path is itself a mount point rather than a plain directory.
 * On Unix, compares the device ID of path against its parent.
 * On Windows, checks that the path is a valid drive root.
 */
bool is_mount_point(const fs::path& path);

}  // namespace caravault

#endif  // CARAVAULT_PLATFORM_HPP
