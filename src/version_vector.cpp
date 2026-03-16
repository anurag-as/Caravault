#include "version_vector.hpp"
#include <algorithm>
#include <set>
#include <sstream>
#include <stdexcept>

namespace caravault {

VersionVector::VersionVector() : clocks_() {}

VersionVector::VersionVector(const std::map<std::string, uint64_t>& clocks)
    : clocks_(clocks) {}

void VersionVector::increment(const std::string& drive_id) {
    clocks_[drive_id]++;
}

void VersionVector::merge(const VersionVector& other) {
    // Take element-wise maximum of all clocks
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
    // Collect all drive IDs from both vectors
    std::set<std::string> all_drives;
    for (const auto& [drive_id, _] : clocks_) {
        all_drives.insert(drive_id);
    }
    for (const auto& [drive_id, _] : other.clocks_) {
        all_drives.insert(drive_id);
    }

    bool this_greater = false;  // ∃ drive: this[drive] > other[drive]
    bool other_greater = false; // ∃ drive: other[drive] > this[drive]

    for (const auto& drive_id : all_drives) {
        uint64_t this_clock = get_clock(drive_id);
        uint64_t other_clock = other.get_clock(drive_id);

        if (this_clock > other_clock) {
            this_greater = true;
        } else if (other_clock > this_clock) {
            other_greater = true;
        }
    }

    // Determine ordering based on comparison results
    if (!this_greater && !other_greater) {
        return Ordering::EQUAL;
    } else if (this_greater && !other_greater) {
        return Ordering::DOMINATES;
    } else if (!this_greater && other_greater) {
        return Ordering::DOMINATED_BY;
    } else {
        // Both this_greater and other_greater are true
        return Ordering::CONCURRENT;
    }
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
        if (!first) {
            oss << ",";
        }
        first = false;
        
        // Escape drive_id for JSON (simple escaping for quotes and backslashes)
        std::string escaped_id = drive_id;
        size_t pos = 0;
        while ((pos = escaped_id.find('\\', pos)) != std::string::npos) {
            escaped_id.replace(pos, 1, "\\\\");
            pos += 2;
        }
        pos = 0;
        while ((pos = escaped_id.find('"', pos)) != std::string::npos) {
            escaped_id.replace(pos, 1, "\\\"");
            pos += 2;
        }
        
        oss << "\"" << escaped_id << "\":" << clock;
    }
    
    oss << "}";
    return oss.str();
}

VersionVector VersionVector::from_json(const std::string& json) {
    std::map<std::string, uint64_t> clocks;
    
    // Simple JSON parser for the specific format: {"key1":value1,"key2":value2,...}
    // This is a minimal parser sufficient for our use case
    
    if (json.empty() || json[0] != '{') {
        throw std::runtime_error("Invalid JSON: must start with '{'");
    }
    
    size_t pos = 1; // Skip opening brace
    
    while (pos < json.length()) {
        // Skip whitespace
        while (pos < json.length() && std::isspace(json[pos])) {
            pos++;
        }
        
        if (pos >= json.length()) {
            throw std::runtime_error("Invalid JSON: unexpected end");
        }
        
        // Check for closing brace
        if (json[pos] == '}') {
            break;
        }
        
        // Parse key (must be quoted string)
        if (json[pos] != '"') {
            throw std::runtime_error("Invalid JSON: expected '\"' for key");
        }
        pos++; // Skip opening quote
        
        std::string key;
        while (pos < json.length() && json[pos] != '"') {
            if (json[pos] == '\\' && pos + 1 < json.length()) {
                // Handle escaped characters
                pos++;
                if (json[pos] == '"' || json[pos] == '\\') {
                    key += json[pos];
                } else {
                    throw std::runtime_error("Invalid JSON: unsupported escape sequence");
                }
            } else {
                key += json[pos];
            }
            pos++;
        }
        
        if (pos >= json.length()) {
            throw std::runtime_error("Invalid JSON: unterminated string");
        }
        pos++; // Skip closing quote
        
        // Skip whitespace
        while (pos < json.length() && std::isspace(json[pos])) {
            pos++;
        }
        
        // Expect colon
        if (pos >= json.length() || json[pos] != ':') {
            throw std::runtime_error("Invalid JSON: expected ':' after key");
        }
        pos++; // Skip colon
        
        // Skip whitespace
        while (pos < json.length() && std::isspace(json[pos])) {
            pos++;
        }
        
        // Parse value (must be number)
        if (pos >= json.length() || !std::isdigit(json[pos])) {
            throw std::runtime_error("Invalid JSON: expected number for value");
        }
        
        uint64_t value = 0;
        while (pos < json.length() && std::isdigit(json[pos])) {
            value = value * 10 + (json[pos] - '0');
            pos++;
        }
        
        clocks[key] = value;
        
        // Skip whitespace
        while (pos < json.length() && std::isspace(json[pos])) {
            pos++;
        }
        
        // Check for comma or closing brace
        if (pos < json.length() && json[pos] == ',') {
            pos++; // Skip comma
        } else if (pos < json.length() && json[pos] == '}') {
            break;
        } else if (pos >= json.length()) {
            throw std::runtime_error("Invalid JSON: unexpected end");
        }
    }
    
    return VersionVector(clocks);
}

bool VersionVector::operator==(const VersionVector& other) const {
    return clocks_ == other.clocks_;
}

bool VersionVector::operator!=(const VersionVector& other) const {
    return !(*this == other);
}

} // namespace caravault
