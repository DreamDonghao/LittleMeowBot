/// @file UsageStore.hpp
/// @brief LLM 用量统计存储
/// @author donghao
/// @date 2026-08-30
/// @details 表：llm_usage（每次 LLM 调用的 token 用量明细）

#pragma once
#include <json/json.h>
#include <string>

namespace insoulforge {
    /// @brief LLM 用量统计存储
    namespace UsageStore {
        /// @brief 记录一次 LLM 调用用量
        void addUsageRecord(const std::string &role, const std::string &model, int promptTokens, int completionTokens,
          int totalTokens, int cachedTokens);

        /// @brief 获取最近 N 天用量汇总（按角色、按天聚合）
        [[nodiscard]] Json::Value getUsageSummary(int days);

        /// @brief 获取最近调用明细
        [[nodiscard]] Json::Value getRecentUsage(int limit);
    } // namespace UsageStore
} // namespace insoulforge