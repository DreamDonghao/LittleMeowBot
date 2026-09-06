/// @file ProcessQQMessages.hpp
/// @brief QQ 消息处理控制器
/// @author donghao
/// @date 2026-04-02

#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace insoulforge {
    /// @brief QQ 消息 HTTP 入口控制器
    /// @details 只负责 OneBot 请求解析与确认，业务处理委托给 MessagePipeline。
    class ProcessQQMessages : public drogon::HttpController<ProcessQQMessages> {
    public:
        ProcessQQMessages() = default;

        ~ProcessQQMessages() override = default;

        METHOD_LIST_BEGIN
        ADD_METHOD_TO(ProcessQQMessages::receiveMessages, "/", drogon::Post);
        METHOD_LIST_END

        /// @brief 接收并处理 QQ 消息
        /// @param req HTTP 请求，包含 OneBot 协议的 JSON 消息
        /// @param callback HTTP 响应回调
        /// @details 有效请求会先返回成功响应，再异步执行消息中间件链路。
        static drogon::Task<> receiveMessages(
          drogon::HttpRequestPtr req, std::function<void(const drogon::HttpResponsePtr &)> callback);
    };
} // namespace insoulforge
