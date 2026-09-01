/// @file ConfigStore.hpp
/// @brief 配置存储
/// @author donghao
/// @date 2026-08-30
/// @details 表：llm_config（多行 LLM 配置）、settings（QQ/记忆配置以 JSON 键值存储）

#pragma once
#include <json/json.h>
#include <string>

namespace insoulforge {
    /// @brief 配置存储
    class ConfigStore {
    public:
        static ConfigStore &instance();

        // ============================================================
        //                      LLM 配置
        // ============================================================

        [[nodiscard]] Json::Value getLLMConfig(const std::string &name) const;

        void saveLLMConfig(const std::string &name, const Json::Value &config) const;

        [[nodiscard]] Json::Value getAllLLMConfigs() const;

        // ============================================================
        //              QQ Bot / 记忆 配置（settings 存储）
        // ============================================================

        [[nodiscard]] Json::Value getQQConfig() const;

        void saveQQConfig(const Json::Value &config) const;

        [[nodiscard]] Json::Value getMemoryConfig() const;

        void saveMemoryConfig(const Json::Value &config) const;

        /// @brief 首次启动时初始化默认 LLM 配置（已存在则跳过）
        void initDefaults() const;

    private:
        ConfigStore() = default;

        /// @brief 读取 settings 中的 JSON 配置键，不存在或解析失败时补齐默认值
        [[nodiscard]] Json::Value loadConfigJson(const std::string &key, const Json::Value &defaults) const;

        void saveConfigJson(const std::string &key, const Json::Value &config) const;
    };
} // namespace insoulforge