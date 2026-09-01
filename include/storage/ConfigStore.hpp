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
    namespace ConfigStore {
        // ============================================================
        //                      LLM 配置
        // ============================================================

        [[nodiscard]] Json::Value getLLMConfig(const std::string &name);

        void saveLLMConfig(const std::string &name, const Json::Value &config);

        [[nodiscard]] Json::Value getAllLLMConfigs();

        // ============================================================
        //              QQ Bot / 记忆 配置（settings 存储）
        // ============================================================

        [[nodiscard]] Json::Value getQQConfig();

        void saveQQConfig(const Json::Value &config);

        [[nodiscard]] Json::Value getMemoryConfig();

        void saveMemoryConfig(const Json::Value &config);

        /// @brief 首次启动时初始化默认 LLM 配置（已存在则跳过）
        void initDefaults();
    } // namespace ConfigStore
} // namespace insoulforge