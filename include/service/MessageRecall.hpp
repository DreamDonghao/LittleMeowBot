/// @file MessageRecall.hpp
/// @brief 消息级长期记忆召回缓存
/// @author donghao
/// @date 2026-09-01
/// @details 消息入库时以消息文本为查询后台召回一次长期记忆（无命中也记键，防止重复召回），
///          Executor 构建提示词时按 message_id 注入 memories 字段；
///          缓存按入库顺序保留最近 kRecentRecordCount 条，滑出即淘汰。

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace insoulforge {
    /// @brief 单条消息召回的长期记忆命中
    struct MessageRecallHit {
        int64_t id; ///< 长期记忆条目 id
        std::string content; ///< 记忆内容
        float similarity; ///< 余弦相似度
    };

    /// @brief 消息级召回缓存（内存态，按会话隔离，线程安全）
    namespace MessageRecall {
        /// @brief 消息入库回调：自发消息或文本过短直接记空键；否则无键时触发后台召回
        /// @param sessionId 会话 ID
        /// @param contentJson 记录的 content JSON 字符串（QQMessage 格式化结果）
        /// @param isAssistant 是否为机器人自发消息
        void onRecordAdded(uint64_t sessionId, const std::string &contentJson, bool isAssistant);

        /// @brief 查询消息的召回结果；键不存在（未完成或向量化失败）返回空
        [[nodiscard]] std::vector<MessageRecallHit> getHits(uint64_t sessionId, uint64_t messageId);
    } // namespace MessageRecall
} // namespace insoulforge
