#include "security/Sha256.h"

#include <array>
#include <iomanip>
#include <sstream>

#include <openssl/evp.h>

#include "log/Log.h"

namespace webdisk {
namespace security {

common::Result<std::string> Sha256::hex(std::string_view content) {
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == nullptr) {
        LOG_ERROR("Failed to allocate SHA-256 context");
        return common::Result<std::string>::failure(500, "Failed to create file hash context");
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;
    const bool ok = EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1 &&
                    EVP_DigestUpdate(context, content.data(), content.size()) == 1 &&
                    EVP_DigestFinal_ex(context, digest.data(), &digest_size) == 1;
    EVP_MD_CTX_free(context);

    if (!ok) {
        LOG_ERROR("SHA-256 digest computation failed");
        return common::Result<std::string>::failure(500, "Failed to compute file hash");
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int index = 0; index < digest_size; ++index) {
        output << std::setw(2) << static_cast<unsigned int>(digest[index]);
    }
    return common::Result<std::string>::success(output.str());
}

} // namespace security
} // namespace webdisk
