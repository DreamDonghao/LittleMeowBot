/// @file ChatRecordStore.hpp
/// @brief 聊天记录存储
/// @author donghao
/// @date 2026-08-30
/// @details 表：chat_records（滑动窗口聊天记录，支持水位线增量读取）

#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include <util/JsonUtil.hpp>

namespace insoulforge {
    /// @brief 聊天记录存储
    namespace ChatRecordStore {
        void addChatRecord(uint64_t sessionId, const std::string &role, const std::string &content);

        [[nodiscard]] std::vector<json> getChatRecords(uint64_t sessionId, int limit = 50);

        [[nodiscard]] std::vector<json> getChatRecordsWithIds(uint64_t sessionId, int limit = 50);

        /// @brief 获取水位线之后的最新记录（旧→新），limit<=0 表示不限
        [[nodiscard]] std::vector<json> getChatRecordsSince(uint64_t sessionId, uint64_t watermarkId, int limit = 0);

        /// @brief 统计水位线之后的记录条数
        [[nodiscard]] size_t getChatRecordCountSince(uint64_t sessionId, uint64_t watermarkId);

        /// @brief 更新聊天记录内容
        void updateChatRecord(int recordId, const std::string &content);

        /// @brief 删除聊天记录
        void deleteChatRecord(int recordId);

        /// @brief 清空群的所有聊天记录
        void clearSessionChatRecords(uint64_t sessionId);
    } // namespace ChatRecordStore
} // namespace insoulforge
