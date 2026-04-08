/// @file PromptService.hpp
/// @brief 提示词服务 - 动态加载与管理 LLM 提示词
/// @author donghao
/// @date 2026-04-02
/// @details 从数据库加载和管理提示词，支持：
///          - 运行时动态修改提示词
///          - 占位符替换（如 {botName}）
///          - 默认提示词初始化

#pragma once
#include <string>

namespace LittleMeowBot {
    /// @brief 提示词服务类（单例模式）
    /// @details 管理所有 LLM 使用的提示词，支持运行时修改
    class PromptService{
    public:
        /// @brief 获取单例实例
        /// @return PromptService 实例引用
        static PromptService& instance();

        /// @brief 初始化提示词（如果不存在则插入默认值）
        void initialize() const;

        /// @brief 获取提示词（支持占位符替换）
        /// @param key 提示词键名
        /// @return 提示词内容，已替换 {botName} 等占位符
        std::string getPrompt(const std::string& key) const;

        /// @brief 设置提示词（运行时修改）
        /// @param key 提示词键名
        /// @param content 提示词内容
        void setPrompt(const std::string& key, const std::string& content) const;

        /// @brief 获取 Executor 系统提示词
        /// @return Executor 角色系统提示词
        std::string getExecutorSystemPrompt() const;

        /// @brief 获取 Executor 提醒提示词
        /// @return Executor 提醒提示词（添加在 assistant 消息后）
        std::string getExecutorRemindPrompt() const;

    private:
        PromptService() = default;
    };
}