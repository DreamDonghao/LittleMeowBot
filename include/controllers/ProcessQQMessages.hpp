/// @file ProcessQQMessages.hpp
/// @brief QQ 消息处理控制器
/// @author donghao
/// @date 2026-04-02

#pragma once

#include <drogon/HttpController.h>
#include <drogon/utils/coroutine.h>

namespace insoulforge {
    /// @brief QQ 消息处理控制器
    /// @details 接收来自 OneBot 协议的 QQ 消息，通过两层 Agent 架构处理：
    ///          1. Router Agent - 判断是否需要回复并规划回复策略
    ///          2. Executor Agent - 生成回复并调用工具
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
        /// @details 处理流程：
        ///          1. 解析消息格式
        ///          2. 检查是否为命令
        ///          3. 检查群是否启用
        ///          4. 记录聊天记录
        ///          5. 调用 Agent 系统生成回复
        ///          6. 触发记忆生成（如果达到阈值）
        static drogon::Task<> receiveMessages(
          drogon::HttpRequestPtr req, std::function<void(const drogon::HttpResponsePtr &)> callback);
    };
} // namespace insoulforge
