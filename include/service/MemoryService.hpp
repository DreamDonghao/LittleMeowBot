/// @file MemoryService.hpp
/// @brief 记忆服务 - 短期记忆生成、合并与迁移
/// @author donghao
/// @date 2026-04-02
/// @details 提供记忆系统的核心功能：
///          - 从聊天记录提取短期记忆
///          - 合并新旧记忆（去重、归纳）
///          - 迁移到长期记忆库（RAGFlow）

#pragma once
#include <api/ApiClient.hpp>
#include <string>

namespace LittleMeowBot {
    /// @brief 记忆服务类
    /// @details 封装短期记忆生成、合并与迁移逻辑，单例模式
    class MemoryService{
    public:
        static MemoryService& instance();

        // ============================================================
        //                      短期记忆操作
        // ============================================================

        /// @brief 获取群的短期记忆文本
        [[nodiscard]] std::string getShortTermMemory(uint64_t groupId) const;

        /// @brief 更新群的短期记忆
        void updateShortTermMemory(uint64_t groupId, const std::string& memoryText) const;

        /// @brief 从聊天记录生成新的记忆条目
        drogon::Task<std::string> generateMemoryItems(const std::string& chatRecords) const;

        /// @brief 合并新旧记忆（去重、归纳）
        drogon::Task<std::string> mergeMemories(
            const std::string& existingMemory,
            const std::string& newMemory) const;

        /// @brief 追加新记忆并合并（完整流程）
        drogon::Task<void> appendAndMergeMemory(uint64_t groupId, const std::string& chatRecords) const;

        // ============================================================
        //                      长期记忆操作
        // ============================================================

        /// @brief 从短期记忆迁移到长期记忆库
        drogon::Task<std::string> migrateToLongTermMemory(
            uint64_t groupId,
            const std::string& shortTermMemory) const;

        /// @brief 筛选值得长期保存的记忆
        drogon::Task<std::string> selectMemoriesToMigrate(const std::string& shortTermMemory) const;

        /// @brief 从文本中删除已迁移的行
        [[nodiscard]] std::string removeMigratedLines(
            const std::string& original,
            const std::string& migrated) const;

        /// @brief 截断记忆到最多 N 条
        [[nodiscard]] std::string trimToMaxLines(const std::string& memory, int maxLines) const;

        /// @brief 统计文本行数
        [[nodiscard]] int countLines(const std::string& text) const;

    private:
        MemoryService() = default;
    };
}