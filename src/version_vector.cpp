#include "version_vector.hpp"
#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

namespace caravault {

VersionVector::VersionVector() = default;

VersionVector::VersionVector(const std::map<std::string, uint64_t>& clocks)
    : clocks_(clocks) {}

void VersionVector::increment(const std::string& drive_id) {
    clocks_[drive_id]++;
}

void VersionVector::merge(const VersionVector& other) {
    for (const auto& [drive_id, clock] : other.clocks_) {
        auto it = clocks_.find(drive_id);
        if (it == clocks_.end()) {
            clocks_[drive_id] = clock;
        } else {
            it->second = std::max(it->second, clock);
        }
    }
}

VersionVector::Ordering VersionVector::compare(const VersionVector& other) const {
    std::set<std::string> all_drives;
    for (const auto& [drive_id, _] : clocks_)       all_drives.insert(drive_id);
    for (const auto& [drive_id, _] : other.clocks_) all_drives.insert(drive_id);

    bool this_greater  = false;
    bool other_greater = false;

    for (const auto& drive_id : all_drives) {
        uint64_t this_clock  = get_clock(drive_id);
        uint64_t other_clock = other.get_clock(drive_id);
        if (this_clock > other_clock)       this_greater  = true;
        else if (other_clock > this_clock)  other_greater = true;
    }

    if (!this_greater && !other_greater) return Ordering::EQUAL;
    if (this_greater  && !other_greater) return Ordering::DOMINATES;
    if (!this_greater && other_greater)  return Ordering::DOMINATED_BY;
    return Ordering::CONCURRENT;
}

uint64_t VersionVector::get_clock(const std::string& drive_id) const {
    auto it = clocks_.find(drive_id);
    return (it != clocks_.end()) ? it->second : 0;
}

const std::map<std::string, uint64_t>& VersionVector::get_clocks() const {
    return clocks_;
}

std::string VersionVector::to_json() const {
    std::ostringstream oss;
    oss << "{";
    bool first = true;
    for (const auto& [drive_id, clock] : clocks_) {
        if (!first) oss << ",";
        first = false;
        oss << "\"";
        for (char c : drive_id) {
            if (c == '\\' || c == '"') oss << '\\';
            oss << c;
        }
        oss << "\":" << clock;
    }
    oss << "}";
    return oss.str();
}

VersionVector VersionVector::from_json(const std::string& json) {
    std::map<std::string, uint64_t> clocks;

    if (json.empty() || json[0] != '{') {
        throw std::runtime_error("Invalid JSON: must start with '{'");
    }

    auto skip_ws = [&](size_t pos) {
        while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
        return pos;
    };

    size_t pos = skip_ws(1);

    while (pos < json.size()) {
        if (json[pos] == '}') break;

        if (json[pos] != '"')
            throw std::runtime_error("Invalid JSON: expected '\"' for key");
        ++pos;

        std::string key;
        while (pos < json.size() && json[pos] != '"') {
            if (json[pos] == '\\') {
                ++pos;
                if (pos >= json.size() || (json[pos] != '"' && json[pos] != '\\'))
                    throw std::runtime_error("Invalid JSON: unsupported escape sequence");
            }
            key += json[pos++];
        }
        if (pos >= json.size())
            throw std::runtime_error("Invalid JSON: unterminated string");
        ++pos; // closing quote

        pos = skip_ws(pos);
        if (pos >= json.size() || json[pos] != ':')
            throw std::runtime_error("Invalid JSON: expected ':' after key");
        pos = skip_ws(pos + 1);

        if (pos >= json.size() || !std::isdigit(static_cast<unsigned char>(json[pos])))
            throw std::runtime_error("Invalid JSON: expected number for value");

        uint64_t value = 0;
        while (pos < json.size() && std::isdigit(static_cast<unsigned char>(json[pos])))
            value = value * 10 + (json[pos++] - '0');

        clocks[key] = value;

        pos = skip_ws(pos);
        if (pos < json.size() && json[pos] == ',') ++pos;
        else if (pos < json.size() && json[pos] == '}') break;
        else if (pos >= json.size()) throw std::runtime_error("Invalid JSON: unexpected end");
    }

    return VersionVector(clocks);
}

bool VersionVector::operator==(const VersionVector& other) const {
    return clocks_ == other.clocks_;
}

} // namespace caravault
