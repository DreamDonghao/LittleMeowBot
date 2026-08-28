/// @file AgentTypes.hpp
/// @brief Agent 类型定义 - 两层代理架构的核心数据结构
/// @details 定义两层代理架构中使用的核心数据类型：
///          - RouterDecision: Router Agent 的路由决策（含回复策略）
///          - ReplyDecision: Executor Agent 的回复结果

#pragma once
#include <json/value.h>
#include <string>
#include <array>
#include <string_view>
#include <fmt/core.h>
#include <format>

namespace insoulforge {
    /// @brief Router Agent 决策结果（合并了规划功能）
    struct RouterDecision {
        enum class Action {
            SKIP, ///< 不处理
            REPLY ///< 需要回复
        };

        Action action = Action::SKIP;
        std::string reason;

        // 回复策略
        bool shouldReply = true;
        bool enableThinking = false;
        std::string tone = "friendly";
        int maxLength = 25;
        bool isPriority = false;
        bool isPrivate = false; ///< 是否私聊会话（决定 Executor 使用私聊人设提示词）

        [[nodiscard]] static constexpr std::string_view actionToString(Action a) {
            constexpr std::array names = {"skip", "reply"};
            return names[static_cast<size_t>(a)];
        }
    };

    /// @brief Executor Agent 回复结果
    struct ReplyDecision {
        bool shouldReply = false;
        std::string content;
    };
}

// fmt::formatter 特化
template<>
struct fmt::formatter<insoulforge::RouterDecision::Action> : formatter<string_view> {
    template<typename FormatContext>
    auto format(insoulforge::RouterDecision::Action a, FormatContext &ctx) const {
        return formatter<string_view>::format(
            insoulforge::RouterDecision::actionToString(a), ctx);
    }
};

// std::formatter 特化
template<>
struct std::formatter<insoulforge::RouterDecision::Action> : std::formatter<std::string_view> {
    template<typename FormatContext>
    auto format(const insoulforge::RouterDecision::Action a, FormatContext &ctx) const {
        return std::formatter<std::string_view>::format(
            insoulforge::RouterDecision::actionToString(a), ctx);
    }
};