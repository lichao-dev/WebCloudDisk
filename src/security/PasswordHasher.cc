#include "security/PasswordHasher.h"

#include <array>
#include <charconv>
#include <limits>
#include <string_view>
#include <vector>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include "log/Log.h"

namespace webdisk {
namespace security {

namespace {

constexpr size_t SALT_SIZE = 16;
constexpr size_t HASH_SIZE = 32;
constexpr std::string_view ALGORITHM = "pbkdf2-sha256";

// 把二进制数据转换成可打印的字符串格式
std::string base64_encode(const unsigned char* data, size_t size) {
    std::string output(4 * ((size + 2) / 3), '\0');
    const int written = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(output.data()), data, static_cast<int>(size));
    output.resize(static_cast<size_t>(written));
    return output;
}

common::Result<std::vector<unsigned char>> base64_decode(const std::string& input) {
    if (input.empty() || input.size() % 4 != 0) {
        return common::Result<std::vector<unsigned char>>::failure(500, "Invalid password hash format");
    }

    std::vector<unsigned char> output((input.size() / 4) * 3);
    const int decoded = EVP_DecodeBlock(output.data(), reinterpret_cast<const unsigned char*>(input.data()),
                                        static_cast<int>(input.size()));
    if (decoded < 0) {
        return common::Result<std::vector<unsigned char>>::failure(500, "Invalid password hash format");
    }

    // EVP_DecodeBlock 返回的长度包含 Base64 尾部 '=' 对应的填充字节，
    // 因此需要根据实际填充数量修正最终长度。
    size_t padding = 0;
    if (!input.empty() && input.back() == '=') {
        ++padding;
    }
    if (input.size() >= 2 && input[input.size() - 2] == '=') {
        ++padding;
    }
    output.resize(static_cast<size_t>(decoded) - padding);
    return common::Result<std::vector<unsigned char>>::success(std::move(output));
}

// 通过 PBKDF2-HMAC-SHA256 算法计算出最终的密码哈希值
common::Result<std::vector<unsigned char>> derive(const std::string& password, const unsigned char* salt,
                                                  size_t salt_size, int iterations) {
    if (password.size() > static_cast<size_t>(std::numeric_limits<int>::max()) ||
        salt_size > static_cast<size_t>(std::numeric_limits<int>::max())) {
        LOG_ERROR("Password hashing input exceeds the supported size");
        return common::Result<std::vector<unsigned char>>::failure(500, "Password input is too long");
    }

    std::vector<unsigned char> output(HASH_SIZE);
    const int ok =
        PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), salt, static_cast<int>(salt_size),
                          iterations, EVP_sha256(), static_cast<int>(output.size()), output.data());
    if (ok != 1) {
        LOG_ERROR("PBKDF2-HMAC-SHA256 derivation failed");
        return common::Result<std::vector<unsigned char>>::failure(500, "Failed to compute password hash");
    }
    return common::Result<std::vector<unsigned char>>::success(std::move(output));
}

std::vector<std::string> split_hash(const std::string& encoded) {
    std::vector<std::string> parts;
    size_t begin = 0;
    while (begin <= encoded.size()) {
        const size_t separator = encoded.find('$', begin);
        if (separator == std::string::npos) {
            parts.push_back(encoded.substr(begin));
            break;
        }
        parts.push_back(encoded.substr(begin, separator - begin));
        begin = separator + 1;
    }
    return parts;
}

} // namespace

PasswordHasher::PasswordHasher(int iterations)
    : iterations_{iterations} {
}

common::Result<std::string> PasswordHasher::hash(const std::string& password) const {
    std::array<unsigned char, SALT_SIZE> salt{};

    // 生成随机字节作为盐值
    if (RAND_bytes(salt.data(), static_cast<int>(salt.size())) != 1) {
        LOG_ERROR("Failed to generate password salt");
        return common::Result<std::string>::failure(500, "Failed to generate password salt");
    }

    auto derived = derive(password, salt.data(), salt.size(), iterations_);
    if (!derived) {
        return common::Result<std::string>::failure(derived.error().status_code, derived.error().message);
    }

    // 将密码哈希参数编码为字符串格式：
    // algorithm$iterations$salt$hash。
    // 该格式包含验证密码所需的全部信息，便于存储到数据库并在登录时重新计算校验。
    std::string encoded;
    encoded.reserve(128);
    encoded.append(ALGORITHM);
    encoded.push_back('$');
    encoded.append(std::to_string(iterations_));
    encoded.push_back('$');
    encoded.append(base64_encode(salt.data(), salt.size()));
    encoded.push_back('$');
    encoded.append(base64_encode(derived.value().data(), derived.value().size()));

    return common::Result<std::string>::success(std::move(encoded));
}

common::Result<bool> PasswordHasher::verify(const std::string& password, const std::string& encoded_hash) const {
    const auto parts = split_hash(encoded_hash);
    if (parts.size() != 4 || parts[0] != ALGORITHM) {
        LOG_ERROR("Stored password hash has an invalid format");
        return common::Result<bool>::failure(500, "Invalid password hash format");
    }

    int stored_iterations = 0;
    const char* begin = parts[1].data();
    const char* end = begin + parts[1].size();
    const auto parsed = std::from_chars(begin, end, stored_iterations);
    // parsed.ec 非空表示整数解析失败；parsed.ptr 未到 end 表示存在未解析的多余字符；
    // stored_iterations 小于 1 表示迭代次数不是正数。
    if (parsed.ec != std::errc{} || parsed.ptr != end || stored_iterations < 1) {
        LOG_ERROR("Stored password hash has an invalid iteration count");
        return common::Result<bool>::failure(500, "Invalid password hash iteration count");
    }

    auto salt = base64_decode(parts[2]);
    auto expected = base64_decode(parts[3]);
    // 盐值和摘要必须能被 Base64 解码，且解码后的字节数必须符合当前哈希格式约定。
    if (!salt || !expected || salt.value().size() != SALT_SIZE || expected.value().size() != HASH_SIZE) {
        LOG_ERROR("Stored password hash has invalid salt or digest data");
        return common::Result<bool>::failure(500, "Invalid password hash format");
    }

    auto actual = derive(password, salt.value().data(), salt.value().size(), stored_iterations);
    if (!actual) {
        return common::Result<bool>::failure(actual.error().status_code, actual.error().message);
    }

    // 普通字符串比较可能在首个不同字节处提前返回并泄露时序信息；
    // CRYPTO_memcmp 会比较完整哈希，避免这种差异。
    const bool matches = CRYPTO_memcmp(actual.value().data(), expected.value().data(), HASH_SIZE) == 0;
    return common::Result<bool>::success(matches);
}

// 哈希格式无效或保存的 PBKDF2 迭代次数低于当前配置时，需要重新生成密码哈希。
bool PasswordHasher::needs_rehash(const std::string& encoded_hash) const {
    const auto parts = split_hash(encoded_hash);
    if (parts.size() != 4 || parts[0] != ALGORITHM) {
        return true;
    }

    int stored_iterations = 0;
    const char* begin = parts[1].data();
    const char* end = begin + parts[1].size();
    const auto parsed = std::from_chars(begin, end, stored_iterations);
    return parsed.ec != std::errc{} || parsed.ptr != end || stored_iterations < iterations_;
}

} // namespace security
} // namespace webdisk
