/// @file ConfigStore.cpp
/// @brief 配置存储 - 实现
/// @author donghao
/// @date 2026-08-30

#include <spdlog/spdlog.h>
#include <storage/ConfigStore.hpp>
#include <storage/Database.hpp>
#include <storage/Statement.hpp>

namespace insoulforge {
    ConfigStore &ConfigStore::instance() {
        static ConfigStore store;
        return store;
    }

    Json::Value ConfigStore::getLLMConfig(const std::string &name) const {
        const auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Json::Value config;

        const Statement stmt(db.handle(), "SELECT api_key, base_url, path, model, max_tokens, temperature, top_p, "
                                          "reasoning_effort FROM llm_config WHERE name = ?");
        stmt.bind(1, name);

        if (stmt.step()) {
            config["apiKey"] = stmt.getText(0);
            config["baseUrl"] = stmt.getText(1);
            config["path"] = stmt.getText(2);
            config["model"] = stmt.getText(3);
            config["maxTokens"] = stmt.getInt(4);
            config["temperature"] = stmt.getDouble(5);
            config["topP"] = stmt.getDouble(6);
            config["reasoningEffort"] = stmt.getText(7);
        }
        return config;
    }

    void ConfigStore::saveLLMConfig(const std::string &name, const Json::Value &config) const {
        const auto &db = Database::instance();
        std::unique_lock lock(db.mutex());

        const Statement stmt(db.handle(),
          "INSERT OR REPLACE INTO llm_config (name, api_key, base_url, path, model, max_tokens, "
          "temperature, top_p, reasoning_effort) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        stmt.bind(1, name);
        stmt.bind(2, config["apiKey"].asString());
        stmt.bind(3, config["baseUrl"].asString());
        stmt.bind(4, config["path"].asString());
        stmt.bind(5, config["model"].asString());
        stmt.bind(6, config["maxTokens"].asInt());
        stmt.bind(7, config["temperature"].asDouble());
        stmt.bind(8, config["topP"].asDouble());
        stmt.bind(9, config.get("reasoningEffort", "").asString());
        stmt.exec();
    }

    Json::Value ConfigStore::getAllLLMConfigs() const {
        const auto &db = Database::instance();
        std::shared_lock lock(db.mutex());
        Json::Value configs;

        const Statement stmt(db.handle(), "SELECT name, api_key, base_url, path, model, max_tokens, temperature, "
                                          "top_p, reasoning_effort FROM llm_config");
        while (stmt.step()) {
            Json::Value cfg;
            cfg["apiKey"] = stmt.getText(1);
            cfg["baseUrl"] = stmt.getText(2);
            cfg["path"] = stmt.getText(3);
            cfg["model"] = stmt.getText(4);
            cfg["maxTokens"] = stmt.getInt(5);
            cfg["temperature"] = stmt.getDouble(6);
            cfg["topP"] = stmt.getDouble(7);
            cfg["reasoningEffort"] = stmt.getText(8);
            configs[stmt.getText(0)] = cfg;
        }
        return configs;
    }

    Json::Value ConfigStore::getKBConfig() const {
        Json::Value defaults;
        defaults["enabled"] = true;
        defaults["apiKey"] = "";
        defaults["baseUrl"] = "";
        defaults["knowledgeDatasetId"] = "";
        defaults["memoryDatasetId"] = "";
        defaults["memoryDocumentId"] = "";
        return loadConfigJson("kb_config", defaults);
    }

    void ConfigStore::saveKBConfig(const Json::Value &config) const {
        saveConfigJson("kb_config", config);
        spdlog::info("知识库配置已保存");
    }

    Json::Value ConfigStore::getQQConfig() const {
        Json::Value defaults;
        defaults["accessToken"] = "";
        defaults["selfQQNumber"] = 0;
        defaults["qqHttpHost"] = "http://127.0.0.1:3000";
        defaults["botName"] = "小喵";
        return loadConfigJson("qq_config", defaults);
    }

    void ConfigStore::saveQQConfig(const Json::Value &config) const {
        saveConfigJson("qq_config", config);
        spdlog::info("QQ Bot 配置已保存");
    }

    Json::Value ConfigStore::getMemoryConfig() const {
        Json::Value defaults;
        defaults["windowTriggerCount"] = 100;
        defaults["windowKeepCount"] = 50;
        defaults["memoryExtractMaxTokens"] = 4000;
        defaults["routerWindowTriggerCount"] = 20;
        defaults["routerWindowKeepCount"] = 10;
        defaults["shortTermMemoryMax"] = 15;
        defaults["memoryMigrateCount"] = 5;
        return loadConfigJson("memory_config", defaults);
    }

    void ConfigStore::saveMemoryConfig(const Json::Value &config) const {
        saveConfigJson("memory_config", config);
        spdlog::info("记忆配置已保存");
    }

    void ConfigStore::initDefaults() const {
        struct DefaultConfig {
            const char *name, *apiKey, *baseUrl, *path, *model;
            int maxTokens;
            double temperature, topP;
        };

        constexpr DefaultConfig defaults[] = {{.name = "router",
                                                .apiKey = "",
                                                .baseUrl = "http://127.0.0.1:3001",
                                                .path = "/v1/chat/completions",
                                                .model = "deepseek-chat",
                                                .maxTokens = 100,
                                                .temperature = 0.3,
                                                .topP = 0.9},
          {.name = "executor",
            .apiKey = "",
            .baseUrl = "http://127.0.0.1:3001",
            .path = "/v1/chat/completions",
            .model = "deepseek-chat",
            .maxTokens = 150,
            .temperature = 0.7,
            .topP = 0.9},
          {.name = "executorThinking",
            .apiKey = "",
            .baseUrl = "http://127.0.0.1:3001",
            .path = "/v1/chat/completions",
            .model = "deepseek-reasoner",
            .maxTokens = 512,
            .temperature = 0.7,
            .topP = 0.9},
          {.name = "image",
            .apiKey = "",
            .baseUrl = "https://dashscope.aliyuncs.com",
            .path = "/compatible-mode/v1/chat/completions",
            .model = "qwen-vl-plus",
            .maxTokens = 1024,
            .temperature = 0.7,
            .topP = 0.9}};

        const auto &db = Database::instance();
        std::unique_lock lock(db.mutex());

        for (const auto &[name, apiKey, baseUrl, path, model, maxTokens, temperature, topP]: defaults) {
            Statement checkStmt(db.handle(), "SELECT COUNT(*) FROM llm_config WHERE name = ?");
            checkStmt.bind(1, name);
            if (checkStmt.step() && checkStmt.getInt(0) > 0) {
                continue; // 已存在，跳过
            }

            const Statement stmt(db.handle(), "INSERT INTO llm_config (name, api_key, base_url, path, model, "
                                              "max_tokens, temperature, top_p) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
            stmt.bind(1, name);
            stmt.bind(2, apiKey);
            stmt.bind(3, baseUrl);
            stmt.bind(4, path);
            stmt.bind(5, model);
            stmt.bind(6, maxTokens);
            stmt.bind(7, temperature);
            stmt.bind(8, topP);
            stmt.exec();
            spdlog::info("已初始化默认 LLM 配置: {}", name);
        }
    }

    Json::Value ConfigStore::loadConfigJson(const std::string &key, const Json::Value &defaults) const {
        const auto &db = Database::instance();
        std::shared_lock lock(db.mutex());

        Json::Value config = defaults;
        const Statement stmt(db.handle(), "SELECT value FROM settings WHERE key = ?");
        stmt.bind(1, key);
        if (stmt.step()) {
            Json::Value parsed;
            Json::CharReaderBuilder builder;
            Json::CharReader *reader = builder.newCharReader();
            const std::string payload = stmt.getText(0);
            std::string errs;
            if (reader->parse(payload.data(), payload.data() + payload.size(), &parsed, &errs) && parsed.isObject()) {
                // 以存储值覆盖默认值
                for (const auto &name: defaults.getMemberNames()) {
                    if (parsed.isMember(name)) {
                        config[name] = parsed[name];
                    }
                }
            } else {
                spdlog::error("settings 配置 {} 解析失败: {}，使用默认值", key, errs);
            }
            delete reader;
        }
        return config;
    }

    void ConfigStore::saveConfigJson(const std::string &key, const Json::Value &config) const {
        const auto &db = Database::instance();
        std::unique_lock lock(db.mutex());

        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        const std::string payload = Json::writeString(builder, config);

        const Statement stmt(db.handle(), "INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)");
        stmt.bind(1, key);
        stmt.bind(2, payload);
        stmt.exec();
    }
} // namespace insoulforge