/// @file Prompt.hpp
/// @brief 提示词构建器 - LLM 提示词组装
/// @author donghao
/// @date 2026-01-20
/// @details 构建 LLM 请求的提示词：
///          - 系统提示词：setSystemPrompt()
///          - 提醒提示词：setRemindPrompt()
///          - 完整提示词：getPrompt()（包含记忆、聊天记录）

#pragma once
#include <json/config.h>
#include <json/value.h>
#include <model/ChatRecordManager.hpp>
#include <model/MemoryManager.hpp>

namespace LittleMeowBot {
    /// @brief 提示词构建类
    /// @details 组装 LLM 请求所需的完整提示词
    class Prompt{
    public:
        /// @brief 构造函数，初始化系统提示词和提醒提示词的角色
        Prompt();

        /// @brief 设置系统提示词
        /// @param systemPrompt 系统提示词内容
        void setSystemPrompt(const Json::String systemPrompt);

        /// @brief 设置提醒提示词
        /// @param remindPrompt 提醒提示词内容
        void setRemindPrompt(const Json::String remindPrompt);

        /// @brief 获取完整提示词
        /// @param chatRecords 聊天记录管理器
        /// @param memory 长期记忆管理器
        /// @return 完整的消息列表 JSON
        [[nodiscard]] Json::Value getPrompt(
            const ChatRecordManager& chatRecords,
            const MemoryManager& memory) const;

    private:
        Json::Value m_system;  ///< 系统提示词消息
        Json::Value m_remind;  ///< 提醒提示词消息
    };
}