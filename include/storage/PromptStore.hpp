/// @file PromptStore.hpp
/// @brief 提示词存储
/// @author donghao
/// @date 2026-08-30
/// @details 表：prompts（可编辑的系统提示词模板）

#pragma once
#include <string>
#include <unordered_map>

namespace insoulforge {
    /// @brief 提示词存储
    class PromptStore {
    public:
        static PromptStore &instance();

        std::string getPrompt(const std::string &key, const std::string &defaultValue = "") const;

        void setPrompt(const std::string &key, const std::string &content, const std::string &description = "");

        bool hasPrompt(const std::string &key) const;

        std::unordered_map<std::string, std::string> getAllPrompts() const;

    private:
        PromptStore() = default;
    };
} // namespace insoulforge