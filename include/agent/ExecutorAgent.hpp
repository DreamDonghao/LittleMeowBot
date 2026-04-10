/// @file ExecutorAgent.hpp
/// @brief Executor Agent - 第三层执行代理
/// @author donghao
/// @date 2026-04-02
/// @details 负责：
///          - 执行 Planner 规划的工具调用
///          - 根据工具结果生成最终回复内容
///          - 使用 Agent 模式让 LLM 选择 reply/no_reply

#pragma once
#include "AgentTypes.hpp"
#include <model/ChatRecordManager.hpp>
#include <model/MemoryManager.hpp>
#include <config/Config.hpp>
#include <drogon/utils/coroutine.h>
#include <json/value.h>
#include <string>
#include <optional>

namespace LittleMeowBot {
    /// @brief Executor Agent - 第三层，执行计划并生成回复
    /// @details 负责：
    ///          - 执行 Planner 规划的工具调用（get_weather/search_web）
    ///          - 根据工具结果生成最终回复内容
    ///          - 使用 Agent 模式让 LLM 选择 reply/no_reply
    class ExecutorAgent{
    public:
        static ExecutorAgent& instance();

        /// @brief 执行计划并生成回复
        /// @param chatRecords 聊天记录管理器
        /// @param memory 长期记忆管理器
        /// @param plan Planner 的规划结果
        /// @param isPriority 是否是高优先级回复（跳过 Planner 的场景）
        /// @return 回复决策
        drogon::Task<std::optional<ReplyDecision>> execute(
            const ChatRecordManager& chatRecords,
            const MemoryManager& memory,
            const PlanResult& plan,
            bool isPriority = false) const;

        /// @brief 高优先级直接回复（跳过 Planner）
        /// @param chatRecords 聊天记录管理器
        /// @param memory 长期记忆管理器
        /// @return 回复决策
        drogon::Task<std::optional<ReplyDecision>> directReply(
            const ChatRecordManager& chatRecords,
            const MemoryManager& memory) const;

    private:
        ExecutorAgent() = default;

        /// @brief 构建 Executor Prompt
        Json::Value buildExecutorPrompt(
            const ChatRecordManager& chatRecords,
            const MemoryManager& memory,
            const PlanResult& plan,
            bool isPriority) const;

        /// @brief 构建直接回复 Prompt（@提及场景）
        Json::Value buildDirectReplyPrompt(
            const ChatRecordManager& chatRecords,
            const MemoryManager& memory) const;

        /// @brief 使用 Agent 模式执行（带 tools）
        drogon::Task<std::optional<ReplyDecision>> executeWithAgent(
            Json::Value messages,
            const LLMApiConfig& apiConfig,
            const LLMModelParams& params,
            uint64_t groupId = 0) const;

        /// @brief 执行思考模型（不带 tools，返回纯文本分析）
        drogon::Task<std::optional<std::string>> executeThinkingOnly(
            Json::Value messages,
            const LLMApiConfig& apiConfig,
            const LLMModelParams& params) const;

        /// @brief 构建思考模型的系统提示词
        [[nodiscard]] std::string getThinkingSystemPrompt() const;

        /// @brief 获取 Executor 系统提示
        [[nodiscard]] std::string getExecutorSystemPrompt(bool isPriority) const;

        /// @brief 获取直接回复系统提示
        [[nodiscard]] std::string getDirectReplySystemPrompt() const;

        /// @brief Intent 转字符串
        std::string intentToString(PlanResult::Intent intent) const;
    };
}
