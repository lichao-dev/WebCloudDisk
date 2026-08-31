#include "common/ConfigCommandLine.h"

#include <string>
#include <string_view>
#include <utility>

namespace webdisk {
namespace common {

Result<ConfigCommandLineOptions> parse_config_command_line(int argc, char* argv[]) {
    ConfigCommandLineOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            if (options.show_help) {
                return Result<ConfigCommandLineOptions>::failure(400, "--help may only be specified once");
            }
            options.show_help = true;
            continue;
        }

        if (argument == "--config" || argument == "-c") {
            if (options.config_file.has_value()) {
                return Result<ConfigCommandLineOptions>::failure(400, "--config may only be specified once");
            }
            if (++index >= argc || std::string_view{argv[index]}.empty() ||
                std::string_view{argv[index]}.front() == '-') {
                return Result<ConfigCommandLineOptions>::failure(400, "--config requires a file path");
            }
            options.config_file = std::filesystem::path{argv[index]};
            continue;
        }

        return Result<ConfigCommandLineOptions>::failure(400, "Unknown argument: " + std::string{argument});
    }

    // Help must remain usable before any configuration or external service is initialized.
    if (!options.show_help && !options.config_file.has_value()) {
        return Result<ConfigCommandLineOptions>::failure(400, "--config is required");
    }
    return Result<ConfigCommandLineOptions>::success(std::move(options));
}

} // namespace common
} // namespace webdisk
