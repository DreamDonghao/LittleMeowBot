/// @file Config.cpp
/// @brief 全局配置管理 - 实现

#include <config/Config.hpp>
#include <storage/Database.hpp>
#include <spdlog/spdlog.h>

namespace LittleMeowBot {
    namespace {
        /// @brief 从数据库加载单个 LLM 配置
        /// @param name 配置名（router/executor/executorThinking/image）
        /// @param apiConfig 输出的 API 配置
        /// @param modelParams 模型参数（可为 nullptr，表示不加载）
        void loadLLMConfig(const std::string_view name, LLMApiConfig& apiConfig, LLMModelParams* modelParams = nullptr){
            const auto cfg = Database::instance().getLLMConfig(std::string(name));
            if (cfg.isNull()) return;

            apiConfig.apiKey = cfg["apiKey"].asString();
            apiConfig.baseUrl = cfg["baseUrl"].asString();
            apiConfig.path = cfg["path"].asString();
            apiConfig.model = cfg["model"].asString();
            if (cfg.isMember("reasoningEffort")) {
                apiConfig.reasoningEffort = cfg["reasoningEffort"].asString();
            }

            if (modelParams) {
                modelParams->maxTokens = cfg["maxTokens"].asInt();
                modelParams->temperature = cfg["temperature"].asFloat();
                modelParams->topP = cfg["topP"].asFloat();
            }
        }
    }
    Config& Config::instance(){
        static Config config{};
        return config;
    }



    void Config::loadFromDatabase(){
        loadLLMConfig("router", router, &routerParams);
        loadLLMConfig("executor", executor, &executorParams);
        loadLLMConfig("executorThinking", executorThinking, &executorThinkingParams);
        loadLLMConfig("image", image);

        // 加载知识库配置
        if (auto kbCfg = Database::instance().getKBConfig();
            !kbCfg.isNull()) {
            knowledgeBase.enabled = kbCfg.get("enabled", true).asBool();
            knowledgeBase.apiKey = kbCfg["apiKey"].asString();
            knowledgeBase.baseUrl = kbCfg["baseUrl"].asString();
            knowledgeBase.knowledgeDatasetId = kbCfg["knowledgeDatasetId"].asString();
            knowledgeBase.memoryDatasetId = kbCfg["memoryDatasetId"].asString();
            if (kbCfg.isMember("memoryDocumentId")) {
                knowledgeBase.memoryDocumentId = kbCfg["memoryDocumentId"].asString();
            }
            spdlog::info("知识库配置已从数据库加载 (enabled={})", knowledgeBase.enabled);
        }

        // 加载记忆配置
        if (auto memCfg = Database::instance().getMemoryConfig();
            !memCfg.isNull()) {
            windowTriggerCount = memCfg["windowTriggerCount"].asInt();
            windowKeepCount = memCfg["windowKeepCount"].asInt();
            memoryExtractMaxTokens = memCfg["memoryExtractMaxTokens"].asInt();
            routerWindowTriggerCount = memCfg["routerWindowTriggerCount"].asInt();
            routerWindowKeepCount = memCfg["routerWindowKeepCount"].asInt();
            shortTermMemoryMax = memCfg["shortTermMemoryMax"].asInt();
            memoryMigrateCount = memCfg["memoryMigrateCount"].asInt();
            // 兜底: 保留条数必须小于触发条数,否则触发后永远删不完
            if (windowTriggerCount <= 0) windowTriggerCount = 100;
            if (windowKeepCount <= 0 || windowKeepCount >= windowTriggerCount) {
                windowKeepCount = windowTriggerCount / 2;
            }
            if (routerWindowTriggerCount <= 0) routerWindowTriggerCount = 20;
            if (routerWindowKeepCount <= 0 || routerWindowKeepCount >= routerWindowTriggerCount) {
                routerWindowKeepCount = routerWindowTriggerCount / 2;
            }
            spdlog::info("记忆配置已从数据库加载");
        }

        // 加载 QQ Bot 配置
        if (auto qqCfg = Database::instance().getQQConfig();
            !qqCfg.isNull()) {
            accessToken = qqCfg["accessToken"].asString();
            selfQQNumber = qqCfg["selfQQNumber"].asInt64();
            qqHttpHost = qqCfg["qqHttpHost"].asString();
            if (qqCfg.isMember("botName")) {
                botName = qqCfg["botName"].asString();
            }
            spdlog::info("QQ Bot 配置已从数据库加载");
        }

        spdlog::info("所有配置已从数据库加载");
    }
}