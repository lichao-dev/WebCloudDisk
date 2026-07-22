#pragma once

#include "common/Result.h"

#include <string>
#include <string_view>

namespace webdisk {
namespace security {

class Sha256 {
public:
    static common::Result<std::string> hex(std::string_view content);
};

} // namespace security
} // namespace webdisk
