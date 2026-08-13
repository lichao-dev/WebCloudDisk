#pragma once

#include "service/AuthService.h"
#include "service/UserService.h"
#include "user_service.srpc.h"

namespace webdisk {
namespace rpcserver {

class UserRpcServiceImpl final : public rpc::UserRpcService::Service {
public:
    UserRpcServiceImpl(const service::AuthService& auth_service, const service::UserService& user_service);

    void Register(rpc::RegisterRequest* request, rpc::RegisterResponse* response, srpc::RPCContext* context) override;
    void Login(rpc::LoginRequest* request, rpc::LoginResponse* response, srpc::RPCContext* context) override;
    void GetCurrentUser(rpc::GetCurrentUserRequest* request, rpc::GetCurrentUserResponse* response,
                        srpc::RPCContext* context) override;

private:
    const service::AuthService& auth_service_;
    const service::UserService& user_service_;
};

} // namespace rpcserver
} // namespace webdisk
