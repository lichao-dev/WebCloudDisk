#pragma once

#include <filesystem>
#include <optional>

#include "common/Result.h"

namespace webdisk {
namespace common {

struct ConfigCommandLineOptions {
    std::optional<std::filesystem::path> config_file;
    bool show_help{false};
};

Result<ConfigCommandLineOptions> parse_config_command_line(int argc, char* argv[]);

} // namespace common
} // namespace webdisk
