#include <cassert>
#include <string>
#include <string_view>

#include "file_service.pb.h"
#include "user_service.pb.h"

int main() {
    webdisk::rpc::RegisterRequest register_request;
    register_request.set_username("alice");
    register_request.set_password("secret");
    register_request.set_confirm("secret");

    std::string serialized;
    assert(register_request.SerializeToString(&serialized));

    webdisk::rpc::RegisterRequest parsed_register;
    assert(parsed_register.ParseFromString(serialized));
    assert(parsed_register.username() == "alice");
    assert(parsed_register.password() == "secret");

    constexpr std::string_view binary_content{"binary\0content", 14};
    webdisk::rpc::UploadFileRequest upload_request;
    upload_request.set_user_id(42);
    upload_request.set_filename("example.bin");
    upload_request.set_content(binary_content.data(), binary_content.size());
    assert(upload_request.SerializeToString(&serialized));

    webdisk::rpc::UploadFileRequest parsed_upload;
    assert(parsed_upload.ParseFromString(serialized));
    assert(parsed_upload.user_id() == 42);
    assert(parsed_upload.filename() == "example.bin");
    assert(parsed_upload.content().size() == binary_content.size());
    assert(parsed_upload.content() == std::string{binary_content});
    return 0;
}
