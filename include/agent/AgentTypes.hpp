/// @file AgentTypes.hpp
/// @brief Agent 类型定义 - 多层代理架构的核心数据结构
/// @author donghao
/// @date 2026-04-02
/// @details 定义三层代理架构中使用的核心数据类型：
///          - RouterDecision: Router Agent 的路由决策结果
///          - PlanResult: Planner Agent 的规划结果
///          - ReplyDecision: Executor Agent 的回复决策
///          同时提供 fmt::formatter 和 std::formatter 特化以支持格式化

#pragma once
#include <json/value.h>
#include <string>
#include <optional>
#include <array>
#include <string_view>
#include <fmt/core.h>
#include <format>

namespace LittleMeowBot {
    /// @brief Router Agent 决策结果
    struct RouterDecision{
        /// @brief 决策类型（强类型枚举）
        enum class Action{
            SKIP, ///< 不处理，跳过
            PRIORITY_REPLY, ///< 高优先级回复（@提及、直接问题）→ 直接进入 Executor
            NORMAL_PROCESS ///< 正常处理 → 进入 Planner
        };

        Action action = Action::SKIP;
        std::string reason; // 决策原因

        /// @brief Action 转字符串
        [[nodiscard]] static constexpr std::string_view actionToString(Action a){
            constexpr std::array names = {"skip", "priority_reply", "normal_process"};
            return names[static_cast<size_t>(a)];
        }
    };

    /// @brief Planner Agent 规划结果
    struct PlanResult{
        /// @brief 用户意图类型（强类型枚举）
        enum class Intent{
            QUESTION, ///< 问题/询问
            CHAT, ///< 闲聊
            HELP, ///< 求助
            ATTACK, ///< 攻击/恶意
            GREETING, ///< 问候
            UNKNOWN ///< 未知
        };

        /// @brief 回复策略
        struct ResponseStrategy{
            bool shouldReply = true;
            bool enableThinking = false;  // 是否启用思考模型
            std::string tone; // friendly, sarcastic, serious, casual
            int maxLength = 25; // 字数限制：25 或 100
            std::string reason; // 回复或不回复的原因
        };

        Intent intent = Intent::UNKNOWN;
        ResponseStrategy strategy;
        std::string contextSummary; // 上下文摘要

        /// @brief Intent 转字符串
        [[nodiscard]] static constexpr std::string_view intentToString(Intent i){
            constexpr std::array names = {"question", "chat", "help", "attack", "greeting", "unknown"};
            return names[static_cast<size_t>(i)];
        }

        /// @brief 字符串转 Intent（支持大小写不敏感）
        [[nodiscard]] static constexpr Intent intentFromString(std::string_view s){
            // 支持大写和小写
            if (s == "QUESTION" || s == "question") return Intent::QUESTION;
            if (s == "CHAT" || s == "chat") return Intent::CHAT;
            if (s == "HELP" || s == "help") return Intent::HELP;
            if (s == "ATTACK" || s == "attack") return Intent::ATTACK;
            if (s == "GREETING" || s == "greeting") return Intent::GREETING;
            return Intent::UNKNOWN;
        }

        /// @brief 从 JSON 字符串解析
        static std::optional<PlanResult> fromJson(const std::string& jsonStr);

        /// @brief 转换为 JSON
        [[nodiscard]] Json::Value toJson() const;
    };

    /// @brief Executor Agent 回复结果（复用现有 ReplyDecision 结构）
    struct ReplyDecision{
        bool shouldReply = false;
        std::string content;
    };
}

// fmt::formatter 特化，用于 spdlog 格式化
template <>
struct fmt::formatter<LittleMeowBot::RouterDecision::Action> : formatter<string_view>{
    template <typename FormatContext>
    auto format(LittleMeowBot::RouterDecision::Action a, FormatContext& ctx) const{
        return fmt::formatter<string_view>::format(LittleMeowBot::RouterDecision::actionToString(a), ctx);
    }
};

template <>
struct fmt::formatter<LittleMeowBot::PlanResult::Intent> : formatter<string_view>{
    template <typename FormatContext>
    auto format(LittleMeowBot::PlanResult::Intent i, FormatContext& ctx) const{
        return fmt::formatter<string_view>::format(LittleMeowBot::PlanResult::intentToString(i), ctx);
    }
};

// std::formatter 特化，用于 std::format 格式化
template <>
struct std::formatter<LittleMeowBot::RouterDecision::Action> : std::formatter<std::string_view>{
    template <typename FormatContext>
    auto format(LittleMeowBot::RouterDecision::Action a, FormatContext& ctx) const{
        return std::formatter<std::string_view>::format(LittleMeowBot::RouterDecision::actionToString(a), ctx);
    }
};

template <>
struct std::formatter<LittleMeowBot::PlanResult::Intent> : std::formatter<std::string_view>{
    template <typename FormatContext>
    auto format(LittleMeowBot::PlanResult::Intent i, FormatContext& ctx) const{
        return std::formatter<std::string_view>::format(LittleMeowBot::PlanResult::intentToString(i), ctx);
    }
};
