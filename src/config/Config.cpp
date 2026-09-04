/// @file Config.cpp
/// @brief 全局配置管理 - 实现

#include <config/Config.hpp>
#include <spdlog/spdlog.h>
#include <storage/ConfigStore.hpp>
#include <util/CommonUtil.hpp>
#include <util/JsonUtil.hpp>

namespace insoulforge {
    namespace {
        /// @brief 从数据库加载单个 LLM 配置
        /// @param name 配置名（router/executor/executorThinking/image）
        /// @param apiConfig 输出的 API 配置
        /// @param modelParams 模型参数（可为 nullptr，表示不加载）
        void loadLLMConfig(
          const std::string_view name, LLMApiConfig &apiConfig, LLMModelParams *modelParams = nullptr) {
            const auto cfg = ConfigStore::getLLMConfig(std::string(name));
            if (cfg.is_null())
                return;

            apiConfig.apiKey = trim(getStr(cfg, "apiKey"));
            apiConfig.baseUrl = trim(getStr(cfg, "baseUrl"));
            apiConfig.path = trim(getStr(cfg, "path"));
            apiConfig.model = trim(getStr(cfg, "model"));
            if (cfg.contains("reasoningEffort")) {
                apiConfig.reasoningEffort = getStr(cfg, "reasoningEffort");
            }

            if (modelParams) {
                modelParams->maxTokens = getInt(cfg, "maxTokens");
                modelParams->temperature = getDouble(cfg, "temperature");
                modelParams->topP = getDouble(cfg, "topP");
            }
        }
    } // namespace

    Config &Config::instance() {
        static Config config{};
        return config;
    }


    void Config::loadFromDatabase() {
        loadLLMConfig("router", router, &routerParams);
        loadLLMConfig("executor", executor, &executorParams);
        loadLLMConfig("executorThinking", executorThinking, &executorThinkingParams);
        loadLLMConfig("image", image);
        loadLLMConfig("embedding", embedding);

        // 加载记忆配置
        if (auto memCfg = ConfigStore::getMemoryConfig(); !memCfg.is_null()) {
            windowTriggerCount = getInt(memCfg, "windowTriggerCount");
            windowKeepCount = getInt(memCfg, "windowKeepCount");
            memoryExtractMaxTokens = getInt(memCfg, "memoryExtractMaxTokens");
            routerWindowTriggerCount = getInt(memCfg, "routerWindowTriggerCount");
            routerWindowKeepCount = getInt(memCfg, "routerWindowKeepCount");
            shortTermMemoryMax = getInt(memCfg, "shortTermMemoryMax");
            longTermRecallThreshold = getDouble(memCfg, "longTermRecallThreshold");
            longTermInjectThreshold = getDouble(memCfg, "longTermInjectThreshold");
            // 兜底: 保留条数必须小于触发条数,否则触发后永远删不完
            if (windowTriggerCount <= 0)
                windowTriggerCount = 100;
            if (windowKeepCount <= 0 || windowKeepCount >= windowTriggerCount) {
                windowKeepCount = windowTriggerCount / 2;
            }
            if (routerWindowTriggerCount <= 0)
                routerWindowTriggerCount = 20;
            if (routerWindowKeepCount <= 0 || routerWindowKeepCount >= routerWindowTriggerCount) {
                routerWindowKeepCount = routerWindowTriggerCount / 2;
            }
            if (longTermRecallThreshold <= 0.0 || longTermRecallThreshold >= 1.0)
                longTermRecallThreshold = 0.65;
            if (longTermInjectThreshold <= 0.0 || longTermInjectThreshold >= 1.0)
                longTermInjectThreshold = 0.45;
            spdlog::info("记忆配置已从数据库加载");
        }

        // 加载 QQ Bot 配置
        if (auto qqCfg = ConfigStore::getQQConfig(); !qqCfg.is_null()) {
            accessToken = trim(getStr(qqCfg, "accessToken"));
            selfQQNumber = getInt64(qqCfg, "selfQQNumber");
            qqHttpHost = trim(getStr(qqCfg, "qqHttpHost"));
            if (qqCfg.contains("botName")) {
                botName = getStr(qqCfg, "botName");
            }
            spdlog::info("QQ Bot 配置已从数据库加载");
        }

        spdlog::info("所有配置已从数据库加载");
    }
} // namespace insoulforge
