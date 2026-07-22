#pragma once

#include "common/Result.h"
#include "model/AuthContext.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace webdisk {
namespace security {

class JwtService {
public:
    JwtService(std::string secret, std::string issuer, std::chrono::seconds token_ttl);

    common::Result<std::string> generate(std::uint64_t user_id) const;
    common::Result<model::AuthContext> verify(const std::string& token) const;

private:
    std::string secret_;
    std::string issuer_;
    std::chrono::seconds token_ttl_;
};

} // namespace security
} // namespace webdisk
