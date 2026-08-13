#include "rpc/UserRpcServiceImpl.h"

#include <functional>
#include <string>
#include <utility>

#include <workflow/WFTaskFactory.h>

#include "common/TaskScheduler.h"

namespace webdisk {
namespace rpcserver {

namespace {

common::TaskScheduler make_task_scheduler(SeriesWork* series) {
    return common::TaskScheduler{
        [series](SubTask* task) { series->push_back(task); },
        [series](std::function<void()> function) {
            series->push_back(WFTaskFactory::create_go_task("webdisk-rpc", std::move(function)));
        },
    };
}

template <typename Response>
void set_error(Response* response, const common::AppError& error) {
    response->mutable_status()->set_code(error.status_code);
    response->mutable_status()->set_message(error.message);
}

void set_user(rpc::UserInfo* output, const model::User& user) {
    output->set_user_id(user.id);
    output->set_username(user.username);
    output->set_created_at(user.created_at);
    output->set_updated_at(user.updated_at);
}

} // namespace

UserRpcServiceImpl::UserRpcServiceImpl(const service::AuthService& auth_service,
                                       const service::UserService& user_service)
    : auth_service_{auth_service},
      user_service_{user_service} {}

void UserRpcServiceImpl::Register(rpc::RegisterRequest* request, rpc::RegisterResponse* response,
                                  srpc::RPCContext* context) {
    const std::string username = request->username();
    const std::string password = request->password();
    const std::string confirm = request->confirm();
    // get_series() 返回当前这一次 RPC 请求所属的 Workflow 串行任务队列
    const common::TaskScheduler scheduler = make_task_scheduler(context->get_series());

    // 密码哈希是 CPU 密集操作，先进入 Workflow 计算队列，再追加数据库任务。
    scheduler.add_compute_task([this, username, password, confirm, scheduler, response] {
        auth_service_.register_user(
            username, password, confirm, scheduler, [response](common::Result<service::RegisteredUser> result) {
                if (!result) {
                    set_error(response, result.error());
                    return;
                }
                // mutable_*() 返回可修改的 Protobuf 子消息，用于填写本次 RPC 响应。
                response->mutable_status()->set_code(0);
                response->mutable_user()->set_user_id(result.value().user_id);
                response->mutable_user()->set_username(result.value().username);
            });
    });
}

void UserRpcServiceImpl::Login(rpc::LoginRequest* request, rpc::LoginResponse* response,
                               srpc::RPCContext* context) {
    const common::TaskScheduler scheduler = make_task_scheduler(context->get_series());
    auth_service_.login(
        request->username(), request->password(), scheduler,
        [response](common::Result<service::LoginResult> result) {
            if (!result) {
                set_error(response, result.error());
                return;
            }
            response->mutable_status()->set_code(0);
            response->set_access_token(result.value().access_token);
            set_user(response->mutable_user(), result.value().user);
        });
}

void UserRpcServiceImpl::GetCurrentUser(rpc::GetCurrentUserRequest* request,
                                        rpc::GetCurrentUserResponse* response, srpc::RPCContext* context) {
    if (request->user_id() == 0) {
        set_error(response, common::AppError{400, "Invalid user ID"});
        return;
    }

    user_service_.get_current_user(
        request->user_id(), make_task_scheduler(context->get_series()),
        [response](common::Result<model::User> result) {
            if (!result) {
                set_error(response, result.error());
                return;
            }
            response->mutable_status()->set_code(0);
            set_user(response->mutable_user(), result.value());
        });
}

} // namespace rpcserver
} // namespace webdisk
