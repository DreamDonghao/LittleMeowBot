/// @file RouterAgent.hpp
/// @brief Router Agent - 消息路由与规划
/// @details 负责判断消息是否需要回复，并规划回复策略：
///          - 硬规则检查（@提及、刷屏、自身消息）- 无需 LLM
///          - LLM 辅助判断（意图分析 + 策略规划）
///          决策结果包含是否回复 + 回复策略（语气、长度、是否启用思考等）

#pragma once
#include <agent/AgentTypes.hpp>
#include <drogon/utils/coroutine.h>
#include <model/QQMessage.hpp>
#include <service/ChatRecordManager.hpp>

namespace insoulforge {
    /// @brief 路由决策与规划
    /// @param chatRecords 聊天记录
    /// @param message QQ 消息
    /// @return 路由决策结果（包含回复策略）
    [[nodiscard]] drogon::Task<RouterDecision> route(const ChatRecordManager &chatRecords, const QQMessage &message);
} // namespace insoulforge
