/// @file ExecutorAgent.hpp
/// @brief Executor Agent - 执行层（生成回复）
/// @details 负责：
///          - 根据 RouterDecision 的策略生成回复
///          - 调用工具获取信息
///          - 使用 Agent 模式让 LLM 生成最终回复

#pragma once
#include "AgentTypes.hpp"
#include <model/ChatRecordManager.hpp>
#include <model/MemoryManager.hpp>
#include <drogon/utils/coroutine.h>
#include <optional>

namespace LittleMeowBot {
    /// @brief Executor Agent - 执行回复生成

    /// @brief 清理模型输出中的工具调用标签等污染内容
    /// @param text 原始内容
    /// @return 清理后的内容
    [[nodiscard]] std::string cleanReplyContent(const std::string &text);

    /// @brief 执行回复生成
    /// @param chatRecords 聊天记录
    /// @param memory 记忆管理器
    /// @param decision Router 的决策结果（包含回复策略）
    /// @return 回复内容
    [[nodiscard]] drogon::Task<std::optional<ReplyDecision> > execute(
        const ChatRecordManager &chatRecords,
        const MemoryManager &memory,
        const RouterDecision &decision);
}