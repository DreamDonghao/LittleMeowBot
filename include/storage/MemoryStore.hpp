/// @file MemoryStore.hpp
/// @brief 短期记忆存储
/// @author donghao
/// @date 2026-08-30
/// @details 表：short_term_memory（记忆内容 + 聊天记录提取水位线）

#pragma once
#include <cstdint>
#include <string>

namespace insoulforge {
    /// @brief 短期记忆存储
    namespace MemoryStore {
        [[nodiscard]] std::string getShortTermMemory(uint64_t sessionId);

        void updateShortTermMemory(uint64_t sessionId, const std::string &memory);

        /// @brief 获取群记忆水位线（最后已提取的聊天记录 id，无记录时为 0）
        [[nodiscard]] uint64_t getMemoryWatermark(uint64_t sessionId);

        /// @brief 原子更新记忆与水位线（单条 upsert 语句，崩溃安全）
        void updateShortTermMemoryWithWatermark(uint64_t sessionId, const std::string &memory, uint64_t watermarkId);
    } // namespace MemoryStore
} // namespace insoulforge