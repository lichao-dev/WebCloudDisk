#pragma once

#include "common/Result.h"

#include <string>

namespace webdisk {
namespace security {

// 密码哈希器
class PasswordHasher {
public:
    explicit PasswordHasher(int iterations);

    // 生成密码哈希
    common::Result<std::string> hash(const std::string& password) const;
    // 检查用户输入的明文密码是否与数据库中的密码哈希匹配
    common::Result<bool> verify(const std::string& password, const std::string& encoded_hash) const;

    // 判断密码哈希是否需要重新哈希
    bool needs_rehash(const std::string& encoded_hash) const;
    // 返回当前 PasswordHasher 使用的迭代次数
    int iterations() const { return iterations_; }

private:
    int iterations_;
};

} // namespace security
} // namespace webdisk
