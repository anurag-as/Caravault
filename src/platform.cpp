#include "platform.hpp"

#include <algorithm>
#include <cstdio>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace caravault {

std::string normalize_path(const std::string& path) {
    std::string result = path;
    std::replace(result.begin(), result.end(), '\\', '/');
    while (result.size() > 1 && result.back() == '/')
        result.pop_back();
    return result;
}

fs::path to_native_path(const std::string& normalized) {
#ifdef _WIN32
    std::string native = normalized;
    std::replace(native.begin(), native.end(), '/', '\\');
    return fs::path(native);
#else
    return fs::path(normalized);
#endif
}

std::string get_platform_name() {
#ifdef _WIN32
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

FilePermissions get_permissions(const fs::path& path) {
    FilePermissions fp{};

#ifdef _WIN32
    std::error_code ec;
    auto perms = fs::status(path, ec).permissions();
    if (ec)
        return fp;

    using P = fs::perms;
    fp.owner_read = (perms & P::owner_read) != P::none;
    fp.owner_write = (perms & P::owner_write) != P::none;
    fp.owner_exec = (perms & P::owner_exec) != P::none;
    fp.group_read = (perms & P::group_read) != P::none;
    fp.group_write = (perms & P::group_write) != P::none;
    fp.group_exec = (perms & P::group_exec) != P::none;
    fp.other_read = (perms & P::others_read) != P::none;
    fp.other_write = (perms & P::others_write) != P::none;
    fp.other_exec = (perms & P::others_exec) != P::none;
    fp.raw_mode = static_cast<uint32_t>(perms);
#else
    struct stat st{};
    if (::stat(path.c_str(), &st) != 0)
        return fp;

    mode_t m = st.st_mode;
    fp.owner_read = (m & S_IRUSR) != 0;
    fp.owner_write = (m & S_IWUSR) != 0;
    fp.owner_exec = (m & S_IXUSR) != 0;
    fp.group_read = (m & S_IRGRP) != 0;
    fp.group_write = (m & S_IWGRP) != 0;
    fp.group_exec = (m & S_IXGRP) != 0;
    fp.other_read = (m & S_IROTH) != 0;
    fp.other_write = (m & S_IWOTH) != 0;
    fp.other_exec = (m & S_IXOTH) != 0;
    fp.raw_mode = static_cast<uint32_t>(m & 07777);
#endif

    return fp;
}

bool set_permissions(const fs::path& path, const FilePermissions& perms) {
#ifdef _WIN32
    std::error_code ec;
    using P = fs::perms;
    auto p = P::none;
    if (perms.owner_read)
        p |= P::owner_read;
    if (perms.owner_write)
        p |= P::owner_write;
    if (perms.owner_exec)
        p |= P::owner_exec;
    if (perms.group_read)
        p |= P::group_read;
    if (perms.group_write)
        p |= P::group_write;
    if (perms.group_exec)
        p |= P::group_exec;
    if (perms.other_read)
        p |= P::others_read;
    if (perms.other_write)
        p |= P::others_write;
    if (perms.other_exec)
        p |= P::others_exec;
    fs::permissions(path, p, ec);
    return !ec;
#else
    mode_t m = 0;
    if (perms.owner_read)
        m |= S_IRUSR;
    if (perms.owner_write)
        m |= S_IWUSR;
    if (perms.owner_exec)
        m |= S_IXUSR;
    if (perms.group_read)
        m |= S_IRGRP;
    if (perms.group_write)
        m |= S_IWGRP;
    if (perms.group_exec)
        m |= S_IXGRP;
    if (perms.other_read)
        m |= S_IROTH;
    if (perms.other_write)
        m |= S_IWOTH;
    if (perms.other_exec)
        m |= S_IXOTH;
    return ::chmod(path.c_str(), m) == 0;
#endif
}

std::string serialize_permissions(const FilePermissions& perms) {
    unsigned owner =
        (perms.owner_read ? 4u : 0u) | (perms.owner_write ? 2u : 0u) | (perms.owner_exec ? 1u : 0u);
    unsigned group =
        (perms.group_read ? 4u : 0u) | (perms.group_write ? 2u : 0u) | (perms.group_exec ? 1u : 0u);
    unsigned other =
        (perms.other_read ? 4u : 0u) | (perms.other_write ? 2u : 0u) | (perms.other_exec ? 1u : 0u);

    char buf[4];
    std::snprintf(buf, sizeof(buf), "%u%u%u", owner, group, other);
    return std::string(buf);
}

FilePermissions deserialize_permissions(const std::string& perms_str) {
    FilePermissions fp{};
    if (perms_str.size() < 3)
        return fp;

    auto digit = [](char c) -> unsigned {
        return (c >= '0' && c <= '7') ? static_cast<unsigned>(c - '0') : 0u;
    };

    unsigned owner = digit(perms_str[perms_str.size() - 3]);
    unsigned group = digit(perms_str[perms_str.size() - 2]);
    unsigned other = digit(perms_str[perms_str.size() - 1]);

    fp.owner_read = (owner & 4u) != 0;
    fp.owner_write = (owner & 2u) != 0;
    fp.owner_exec = (owner & 1u) != 0;
    fp.group_read = (group & 4u) != 0;
    fp.group_write = (group & 2u) != 0;
    fp.group_exec = (group & 1u) != 0;
    fp.other_read = (other & 4u) != 0;
    fp.other_write = (other & 2u) != 0;
    fp.other_exec = (other & 1u) != 0;
    fp.raw_mode = (owner << 6) | (group << 3) | other;
    return fp;
}

bool is_mount_point(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
        return false;

#ifdef _WIN32
    std::wstring ws = path.wstring();
    if (ws.size() >= 3 && ws[1] == L':' && (ws[2] == L'\\' || ws[2] == L'/'))
        return GetDriveTypeW(ws.c_str()) != DRIVE_NO_ROOT_DIR;
    return false;
#else
    struct stat st_self{}, st_parent{};
    if (::stat(path.c_str(), &st_self) != 0)
        return false;

    fs::path parent = path.parent_path();
    if (parent == path)
        return true;  // filesystem root

    if (::stat(parent.c_str(), &st_parent) != 0)
        return false;

    return st_self.st_dev != st_parent.st_dev;
#endif
}

std::vector<DriveMount> detect_drives() {
    std::vector<DriveMount> drives;

#ifdef _WIN32
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        wchar_t root[4] = {letter, L':', L'\\', L'\0'};
        UINT type = GetDriveTypeW(root);
        if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED)
            continue;

        DriveMount dm;
        dm.drive_id_hint = std::string(1, static_cast<char>(letter)) + ":";
        dm.mount_point = fs::path(root);

        ULARGE_INTEGER free_bytes{}, total_bytes{};
        if (GetDiskFreeSpaceExW(root, nullptr, &total_bytes, &free_bytes)) {
            dm.total_bytes = total_bytes.QuadPart;
            dm.free_bytes = free_bytes.QuadPart;
        }
        drives.push_back(std::move(dm));
    }

#elif defined(__APPLE__)
    const fs::path volumes_dir = "/Volumes";
    std::error_code ec;
    if (!fs::exists(volumes_dir, ec) || ec)
        return drives;

    for (const auto& entry : fs::directory_iterator(volumes_dir, ec)) {
        if (ec)
            break;
        if (!entry.is_directory())
            continue;
        if (!is_mount_point(entry.path()))
            continue;

        DriveMount dm;
        dm.drive_id_hint = entry.path().filename().string();
        dm.mount_point = entry.path();

        std::error_code space_ec;
        auto space = fs::space(entry.path(), space_ec);
        if (!space_ec) {
            dm.total_bytes = space.capacity;
            dm.free_bytes = space.free;
        }
        drives.push_back(std::move(dm));
    }

#else
    for (const char* base : {"/mnt", "/media"}) {
        fs::path base_dir(base);
        std::error_code ec;
        if (!fs::exists(base_dir, ec) || ec)
            continue;

        for (const auto& entry : fs::directory_iterator(base_dir, ec)) {
            if (ec)
                break;
            if (!entry.is_directory())
                continue;
            if (!is_mount_point(entry.path()))
                continue;

            DriveMount dm;
            dm.drive_id_hint = entry.path().filename().string();
            dm.mount_point = entry.path();

            std::error_code space_ec;
            auto space = fs::space(entry.path(), space_ec);
            if (!space_ec) {
                dm.total_bytes = space.capacity;
                dm.free_bytes = space.free;
            }
            drives.push_back(std::move(dm));
        }
    }
#endif

    return drives;
}

}  // namespace caravault
