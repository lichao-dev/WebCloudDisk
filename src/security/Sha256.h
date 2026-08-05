#pragma once

#include <string>
#include <string_view>

#include "common/Result.h"

namespace webdisk {
namespace security {

class Sha256 {
public:
    // 计算内容的 SHA-256 哈希，返回由 64 个小写十六进制字符组成的字符串，供内容寻址存储使用
    static common::Result<std::string> hex(std::string_view content);
};

} // namespace security
} // namespace webdisk
