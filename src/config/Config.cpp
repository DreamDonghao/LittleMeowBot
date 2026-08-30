/// @file Config.cpp
/// @brief 全局配置管理 - 实现

#include <config/Config.hpp>
#include <spdlog/spdlog.h>
#include <cctype>
#include <storage/ConfigStore.hpp>

namespace insoulforge {
    namespace {
        /// @brief 去除配置值首尾空白，避免从后台复制粘贴时带入空格
        std::string trim(std::string s) {
            const auto isSpace = [](const unsigned char c) { return std::isspace(c); };
            while (!s.empty() && isSpace(s.front())) s.erase(s.begin());
            while (!s.empty() && isSpace(s.back())) s.pop_back();
            return s;
        }

        /// @brief 从数据库加载单个 LLM 配置
        /// @param name 配置名（router/executor/executorThinking/image）
        /// @param apiConfig 输出的 API 配置
        /// @param modelParams 模型参数（可为 nullptr，表示不加载）
        void loadLLMConfig(const std::string_view name, LLMApiConfig &apiConfig,
                           LLMModelParams *modelParams = nullptr) {
            const auto cfg = ConfigStore::instance().getLLMConfig(std::string(name));
            if (cfg.isNull()) return;

            apiConfig.apiKey = trim(cfg["apiKey"].asString());
            apiConfig.baseUrl = trim(cfg["baseUrl"].asString());
            apiConfig.path = trim(cfg["path"].asString());
            apiConfig.model = trim(cfg["model"].asString());
            if (cfg.isMember("reasoningEffort")) {
                apiConfig.reasoningEffort = cfg["reasoningEffort"].asString();
            }

            if (modelParams) {
                modelParams->maxTokens = cfg["maxTokens"].asInt();
                modelParams->temperature = cfg["temperature"].asDouble();
                modelParams->topP = cfg["topP"].asDouble();
            }
        }
    }

    Config &Config::instance() {
        static Config config{};
        return config;
    }


    void Config::loadFromDatabase() {
        loadLLMConfig("router", router, &routerParams);
        loadLLMConfig("executor", executor, &executorParams);
        loadLLMConfig("executorThinking", executorThinking, &executorThinkingParams);
        loadLLMConfig("image", image);

        // 加载知识库配置
        if (auto kbCfg = ConfigStore::instance().getKBConfig();
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
        if (auto memCfg = ConfigStore::instance().getMemoryConfig();
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
        if (auto qqCfg = ConfigStore::instance().getQQConfig();
            !qqCfg.isNull()) {
            accessToken = trim(qqCfg["accessToken"].asString());
            selfQQNumber = qqCfg["selfQQNumber"].asInt64();
            qqHttpHost = trim(qqCfg["qqHttpHost"].asString());
            if (qqCfg.isMember("botName")) {
                botName = qqCfg["botName"].asString();
            }
            spdlog::info("QQ Bot 配置已从数据库加载");
        }

        spdlog::info("所有配置已从数据库加载");
    }
}