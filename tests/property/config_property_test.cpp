#include "config.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

namespace fs = std::filesystem;
using namespace caravault;

namespace {

fs::path write_temp_config(const std::string& content) {
    static std::atomic<int> counter{0};
    fs::path p = fs::temp_directory_path() /
                 ("caravault_cfg_" + std::to_string(counter.fetch_add(1)) + ".conf");
    std::ofstream f(p);
    f << content;
    return p;
}

// Generates a non-empty alphanumeric string safe for use as a config value.
rc::Gen<std::string> gen_identifier() {
    return rc::gen::map(rc::gen::string<std::string>(), [](std::string s) {
        std::string result;
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c)))
                result += c;
        }
        return result.empty() ? std::string("x") : result;
    });
}

rc::Gen<double> gen_valid_quorum() {
    return rc::gen::map(rc::gen::inRange<int>(1, 99),
                        [](int v) { return static_cast<double>(v) / 100.0; });
}

rc::Gen<double> gen_invalid_quorum() {
    return rc::gen::oneOf(rc::gen::just(0.0),
                          rc::gen::just(1.0),
                          rc::gen::just(-0.5),
                          rc::gen::just(1.5),
                          rc::gen::just(-100.0));
}

}  // namespace

// For any set of exclusion patterns written to a config file, every pattern
// SHALL be present in the loaded Config::exclude_patterns.
RC_GTEST_PROP(ConfigProperty, ExclusionPatternRespect, ()) {
    auto patterns = *rc::gen::container<std::vector<std::string>>(gen_identifier());
    RC_PRE(!patterns.empty());

    std::string csv;
    for (size_t i = 0; i < patterns.size(); ++i) {
        if (i > 0)
            csv += ',';
        csv += patterns[i];
    }
    fs::path cfg = write_temp_config("exclude_patterns=" + csv + "\n");
    Config c;
    c.load_from_file(cfg);
    fs::remove(cfg);

    for (const auto& pat : patterns) {
        bool found = false;
        for (const auto& loaded : c.exclude_patterns) {
            if (loaded == pat) {
                found = true;
                break;
            }
        }
        RC_ASSERT(found);
    }
}

// For any log level and log file path written to a config file, the loaded
// Config SHALL reflect those exact values.
RC_GTEST_PROP(ConfigProperty, LogConfigurationApplication, ()) {
    Config::LogLevel levels[] = {Config::LogLevel::ERROR,
                                 Config::LogLevel::WARNING,
                                 Config::LogLevel::INFO,
                                 Config::LogLevel::DEBUG};
    Config::LogLevel chosen = levels[*rc::gen::inRange<int>(0, 4)];
    std::string log_path = "/tmp/" + *gen_identifier() + ".log";

    fs::path cfg = write_temp_config("log_level=" + Config::log_level_to_string(chosen) +
                                     "\nlog_file_path=" + log_path + "\n");
    Config c;
    c.load_from_file(cfg);
    fs::remove(cfg);

    RC_ASSERT(c.log_level == chosen);
    RC_ASSERT(c.log_file_path == fs::path(log_path));
}

// For any hash algorithm string written to a config file, the loaded Config
// SHALL store that exact string.
RC_GTEST_PROP(ConfigProperty, HashAlgorithmConfiguration, ()) {
    auto algo = *gen_identifier();
    fs::path cfg = write_temp_config("hash_algorithm=" + algo + "\n");
    Config c;
    c.load_from_file(cfg);
    fs::remove(cfg);

    RC_ASSERT(c.hash_algorithm == algo);
}

// For any quorum threshold in (0, 1) written to a config file, the loaded
// Config SHALL store a value within 1e-6 of the written value.
RC_GTEST_PROP(ConfigProperty, QuorumThresholdConfiguration, ()) {
    double threshold = *gen_valid_quorum();

    std::ostringstream oss;
    oss.precision(10);
    oss << std::fixed << threshold;

    fs::path cfg = write_temp_config("quorum_threshold=" + oss.str() + "\n");
    Config c;
    c.load_from_file(cfg);
    fs::remove(cfg);

    RC_ASSERT(c.quorum_threshold > 0.0);
    RC_ASSERT(c.quorum_threshold < 1.0);
    RC_ASSERT(std::abs(c.quorum_threshold - threshold) < 1e-6);
}

// For any invalid configuration value, the loaded Config SHALL fall back to
// the default value for that setting.
RC_GTEST_PROP(ConfigProperty, InvalidConfigurationFallback, ()) {
    double bad = *gen_invalid_quorum();
    std::ostringstream oss;
    oss << bad;

    fs::path cfg = write_temp_config("quorum_threshold=" + oss.str() + "\n");
    Config c;
    c.load_from_file(cfg);
    fs::remove(cfg);

    RC_ASSERT(c.quorum_threshold == 0.5);

    fs::path cfg2 = write_temp_config("cdc_chunk_size=0\nlarge_file_threshold=-1\n");
    Config c2;
    c2.load_from_file(cfg2);
    fs::remove(cfg2);

    RC_ASSERT(c2.cdc_chunk_size == size_t{64 * 1024});
    RC_ASSERT(c2.large_file_threshold == size_t{1024 * 1024});
}

// For any Config with out-of-range values, validate() SHALL reset them to
// defaults and return false.
RC_GTEST_PROP(ConfigProperty, ConfigurationValidation, ()) {
    Config c;
    c.quorum_threshold = *gen_invalid_quorum();
    c.cdc_chunk_size = 0;
    c.large_file_threshold = 0;
    c.log_rotation_size = 0;
    c.hash_algorithm = "";
    c.manifest_db_name = "";
    c.log_file_path = "";

    RC_ASSERT(!c.validate());

    RC_ASSERT(c.quorum_threshold > 0.0 && c.quorum_threshold < 1.0);
    RC_ASSERT(c.cdc_chunk_size > size_t{0});
    RC_ASSERT(c.large_file_threshold > size_t{0});
    RC_ASSERT(c.log_rotation_size > size_t{0});
    RC_ASSERT(!c.hash_algorithm.empty());
    RC_ASSERT(!c.manifest_db_name.empty());
    RC_ASSERT(!c.log_file_path.empty());
}
