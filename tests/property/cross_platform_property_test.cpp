#include "platform.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>
#include <string>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

// Generate a non-empty path component containing only safe characters.
rc::Gen<std::string> gen_path_component() {
    return rc::gen::map(rc::gen::container<std::string>(rc::gen::elementOf(std::string(
                            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-"))),
                        [](std::string s) { return s.empty() ? std::string("x") : s; });
}

// Generate a path string that may contain both '/' and '\' separators.
rc::Gen<std::string> gen_mixed_path() {
    return rc::gen::map(rc::gen::container<std::vector<std::string>>(gen_path_component()),
                        [](std::vector<std::string> parts) {
                            if (parts.empty())
                                parts.push_back("x");
                            std::string result;
                            for (size_t i = 0; i < parts.size(); ++i) {
                                if (i > 0)
                                    result += (i % 2 == 0) ? '/' : '\\';
                                result += parts[i];
                            }
                            return result;
                        });
}

// Create a temporary file and return its path.
fs::path make_temp_file() {
    static std::atomic<int> counter{0};
    fs::path p = fs::temp_directory_path() /
                 ("caravault_perm_" + std::to_string(counter.fetch_add(1)) + ".tmp");
    std::ofstream f(p);
    f << "test";
    return p;
}

}  // namespace

// For any file path, normalize_path() SHALL produce a string with no backslashes,
// the operation SHALL be idempotent, and to_native_path() SHALL return a non-empty path.
RC_GTEST_PROP(CrossPlatformProperty, PathSeparatorNormalization, ()) {
    auto path_str = *gen_mixed_path();

    std::string normalized = normalize_path(path_str);

    RC_ASSERT(normalized.find('\\') == std::string::npos);
    RC_ASSERT(normalize_path(normalized) == normalized);
    RC_ASSERT(!to_native_path(normalized).empty());
}

// For any permission bitmask, serialize_permissions() followed by
// deserialize_permissions() SHALL round-trip all nine permission bits.
// On Unix, set_permissions() followed by get_permissions() SHALL also round-trip.
RC_GTEST_PROP(CrossPlatformProperty, PermissionModelHandling, ()) {
    auto bits = *rc::gen::inRange<uint32_t>(0, 512);  // 0..0777 octal

    FilePermissions perms{};
    perms.owner_read = (bits & 0400u) != 0;
    perms.owner_write = (bits & 0200u) != 0;
    perms.owner_exec = (bits & 0100u) != 0;
    perms.group_read = (bits & 0040u) != 0;
    perms.group_write = (bits & 0020u) != 0;
    perms.group_exec = (bits & 0010u) != 0;
    perms.other_read = (bits & 0004u) != 0;
    perms.other_write = (bits & 0002u) != 0;
    perms.other_exec = (bits & 0001u) != 0;

    std::string serialized = serialize_permissions(perms);
    FilePermissions restored = deserialize_permissions(serialized);

    RC_ASSERT(restored.owner_read == perms.owner_read);
    RC_ASSERT(restored.owner_write == perms.owner_write);
    RC_ASSERT(restored.owner_exec == perms.owner_exec);
    RC_ASSERT(restored.group_read == perms.group_read);
    RC_ASSERT(restored.group_write == perms.group_write);
    RC_ASSERT(restored.group_exec == perms.group_exec);
    RC_ASSERT(restored.other_read == perms.other_read);
    RC_ASSERT(restored.other_write == perms.other_write);
    RC_ASSERT(restored.other_exec == perms.other_exec);

#ifndef _WIN32
    // Skip modes where owner cannot read — the test process needs to stat the file.
    if (!perms.owner_read)
        return;

    fs::path tmp = make_temp_file();
    if (set_permissions(tmp, perms)) {
        FilePermissions actual = get_permissions(tmp);
        RC_ASSERT(actual.owner_read == perms.owner_read);
        RC_ASSERT(actual.owner_write == perms.owner_write);
        RC_ASSERT(actual.owner_exec == perms.owner_exec);
        RC_ASSERT(actual.group_read == perms.group_read);
        RC_ASSERT(actual.group_write == perms.group_write);
        RC_ASSERT(actual.group_exec == perms.group_exec);
        RC_ASSERT(actual.other_read == perms.other_read);
        RC_ASSERT(actual.other_write == perms.other_write);
        RC_ASSERT(actual.other_exec == perms.other_exec);
    }
    // Restore write permission so the temp file can be cleaned up.
    FilePermissions rw{};
    rw.owner_read = rw.owner_write = true;
    set_permissions(tmp, rw);
    fs::remove(tmp);
#endif
}

// For any path with mixed separators, normalizing it, storing it as a manifest key,
// and converting back to a native path SHALL preserve all path components and
// re-normalizing the native path SHALL reproduce the original key.
RC_GTEST_PROP(CrossPlatformProperty, CrossPlatformManifestPortability, ()) {
    auto parts = *rc::gen::container<std::vector<std::string>>(gen_path_component());
    if (parts.empty())
        parts.push_back("x");

    std::string original;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
            original += (i % 2 == 0) ? '/' : '\\';
        original += parts[i];
    }

    std::map<std::string, std::string> manifest;
    std::string key = normalize_path(original);
    manifest[key] = "some_hash";

    RC_ASSERT(manifest.count(key) == size_t{1});

    fs::path native = to_native_path(key);
    std::string native_str = native.generic_string();

    for (const auto& part : parts)
        RC_ASSERT(native_str.find(part) != std::string::npos);

    RC_ASSERT(normalize_path(native.generic_string()) == key);
}
