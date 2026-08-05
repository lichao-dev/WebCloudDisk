#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace webdisk {
namespace common {

struct AppError {
    // 第一期直接使用 HTTP 状态码表示错误类别，Handler 可据此生成响应：
    // 400：请求参数或请求格式错误
    // 401：身份认证失败，例如密码或访问令牌无效
    // 404：请求的用户或文件不存在
    // 409：资源冲突，例如用户名或文件名已经存在
    // 413：上传文件超过配置的大小限制
    // 500：数据库、文件系统、密码算法或配置等服务器内部错误
    // 启动阶段尚未进入 HTTP 请求处理时，也使用 500 表示服务器内部错误。
    int status_code;
    std::string message;
};

template <typename T>
class Result {
public:
    // 创建成功结果
    static Result success(T value) { return Result(std::move(value)); }
    // 创建失败结果
    static Result failure(int status_code, std::string message) {
        return Result(AppError{status_code, std::move(message)});
    }

    // 判断 variant 当前是否保存的是 T 类型。
    // std::variant<T, AppError> 同一时间只会保存其中一种类型：
    // - 保存 T 表示成功
    // - 保存 AppError 表示失败
    // holds_alternative<T>() 用于检查当前存储的类型是否为 T。
    bool ok() const { return std::holds_alternative<T>(value_); }

    explicit operator bool() const { return ok(); }

    const T& value() const { return std::get<T>(value_); }
    T& value() { return std::get<T>(value_); }
    T&& take_value() { return std::move(std::get<T>(value_)); }
    const AppError& error() const { return std::get<AppError>(value_); }

private:
    explicit Result(T value)
        : value_{std::move(value)} {}
    explicit Result(AppError error)
        : value_{std::move(error)} {}

    // 存储 Result 的结果状态。
    // std::variant<T, AppError> 表示两种互斥状态：
    // - 保存 T：表示操作成功，T 为返回值
    // - 保存 AppError：表示操作失败，包含错误码和错误信息
    // 同一时间 variant 只会保存其中一种类型。
    std::variant<T, AppError> value_;
};

template <>
class Result<void> {
public:
    // 创建成功结果
    static Result success() { return Result(); }
    // 创建失败结果
    static Result failure(int status_code, std::string message) {
        return Result(AppError{status_code, std::move(message)});
    }

    bool ok() const { return !error_.has_value(); }

    explicit operator bool() const { return ok(); }

    const AppError& error() const { return *error_; }

private:
    Result() = default;
    explicit Result(AppError error)
        : error_{std::move(error)} {}

    std::optional<AppError> error_;
};

} // namespace common
} // namespace webdisk
