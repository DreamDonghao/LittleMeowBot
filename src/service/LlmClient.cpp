/// @file LlmClient.cpp
/// @brief API 客户端 - 实现

#include <config/Config.hpp>
#include <service/LlmClient.hpp>
#include <service/WebSocketManager.hpp>
#include <spdlog/spdlog.h>
#include <storage/UsageStore.hpp>
#include <util/CommonUtil.hpp>
#include <util/HttpUtil.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    namespace {
        /// @brief 通用 API 请求函数
        drogon::Task<std::optional<std::string>> requestStr(const Json::Value &messages, const std::string &base_url,
          const std::string &path, const std::string &api_key, const std::string &model, const double temperature,
          const double top_p, const int max_tokens, const std::string &role, const std::optional<uint64_t> sessionId) {
            const LLMApiConfig api{.apiKey = api_key, .baseUrl = base_url, .path = path, .model = model};
            const LLMModelParams params{.maxTokens = max_tokens, .temperature = temperature, .topP = top_p};
            const Json::Value body = LlmClient::buildChatRequestBody(api, params, messages);
            const auto resp =
              co_await HttpUtil::send("[LLM]", base_url, path, drogon::Post, body, api_key, 90.0, sessionId);
            if (!resp) {
                co_return std::nullopt;
            }

            const auto json = LlmClient::validChatJson(*resp);
            if (!json) {
                if (sessionId) {
                    Logger::session(*sessionId)
                      .error("[LLM] 请求出错: status={}", static_cast<int>((*resp)->getStatusCode()));
                } else {
                    spdlog::error("[LLM] 请求出错: status={}", static_cast<int>((*resp)->getStatusCode()));
                }
                co_return std::nullopt;
            }

            LlmClient::logUsage(*json, model, role, sessionId);

            const auto &choices = (*json)["choices"];
            if (!choices.isArray() || choices.empty()) {
                if (sessionId) {
                    Logger::session(*sessionId).error("LLM 返回格式错误: choices 不是数组或为空");
                } else {
                    spdlog::error("LLM 返回格式错误: choices 不是数组或为空");
                }
                co_return std::nullopt;
            }

            co_return choices[0]["message"]["content"].asString();
        }
    } // namespace

    namespace LlmClient {
        Json::Value buildChatRequestBody(const LLMApiConfig &api, const LLMModelParams &params,
          const Json::Value &messages, const Json::Value &tools) {
            Json::Value body;
            body["model"] = api.model;
            body["messages"] = messages;
            if (!tools.isNull())
                body["tools"] = tools;
            body["temperature"] = params.temperature;
            body["max_tokens"] = params.maxTokens;
            body["top_p"] = params.topP;
            if (!api.reasoningEffort.empty())
                body["reasoning_effort"] = api.reasoningEffort;
            return body;
        }

        std::optional<Json::Value> validChatJson(const drogon::HttpResponsePtr &resp) {
            if (!resp || resp->getStatusCode() != drogon::k200OK)
                return std::nullopt;
            const auto json = resp->getJsonObject();
            if (!json || !json->isMember("choices"))
                return std::nullopt;
            return *json;
        }
    } // namespace LlmClient

    drogon::Task<std::optional<std::string>> LlmClient::requestLLM(const Json::Value &messages,
      const double temperature, const double top_p, const int max_tokens, const std::string &role,
      const std::optional<uint64_t> sessionId) {
        const auto &config = Config::instance();
        co_return co_await requestStr(messages, config.executor.baseUrl, config.executor.path, config.executor.apiKey,
          config.executor.model, temperature, top_p, max_tokens, role, sessionId);
    }

    drogon::Task<std::optional<std::vector<float>>> LlmClient::requestEmbedding(
      const std::string &text, const std::optional<uint64_t> sessionId) {
        const auto &config = Config::instance().embedding;
        if (config.baseUrl.empty() || config.model.empty()) {
            spdlog::debug("Embedding 未配置，跳过向量化");
            co_return std::nullopt;
        }

        Json::Value body;
        body["model"] = config.model;
        body["input"].append(text);

        const auto resp = co_await HttpUtil::send(
          "[Embedding]", config.baseUrl, config.path, drogon::Post, body, config.apiKey, 30.0, sessionId);
        if (!resp) {
            co_return std::nullopt;
        }

        const auto json = (*resp)->getJsonObject();
        if ((*resp)->getStatusCode() != drogon::k200OK || !json || !json->isMember("data") ||
            !(*json)["data"].isArray() || (*json)["data"].empty()) {
            if (sessionId) {
                Logger::session(*sessionId)
                  .error("[Embedding] 请求出错: status={}", static_cast<int>((*resp)->getStatusCode()));
            } else {
                spdlog::error("[Embedding] 请求出错: status={}", static_cast<int>((*resp)->getStatusCode()));
            }
            co_return std::nullopt;
        }

        LlmClient::logUsage(*json, config.model, "embedding", sessionId);

        std::vector<float> embedding;
        for (const auto &value: (*json)["data"][0]["embedding"]) {
            embedding.push_back(value.asFloat());
        }
        if (embedding.empty()) {
            co_return std::nullopt;
        }
        co_return embedding;
    }

    void LlmClient::logUsage(const Json::Value &responseJson, const std::string &model, const std::string &role,
      const std::optional<uint64_t> sessionId) {
        if (!responseJson.isMember("usage"))
            return;
        const auto &usage = responseJson["usage"];
        int promptTokens = usage.get("prompt_tokens", 0).asInt();
        int completionTokens = usage.get("completion_tokens", 0).asInt();
        int totalTokens = usage.get("total_tokens", 0).asInt();

        int cachedTokens = 0;
        // OpenAI-compatible: prompt_tokens_details.cached_tokens（0 命中也是有效数据，不能当作缺失）
        if (usage.isMember("prompt_tokens_details")) {
            const auto &details = usage["prompt_tokens_details"];
            cachedTokens = details.get("cached_tokens", 0).asInt();
        } else {
            // 部分网关用 prompt_cache_hit_tokens / prompt_cache_miss_tokens 表达
            const int hitTokens = usage.get("prompt_cache_hit_tokens", 0).asInt();
            const int missTokens = usage.get("prompt_cache_miss_tokens", 0).asInt();
            cachedTokens = hitTokens;
            if (promptTokens == 0) {
                promptTokens = hitTokens + missTokens;
            }
        }

        const auto log = sessionId.has_value() ? std::optional(Logger::session(*sessionId)) : std::nullopt;
        if (promptTokens > 0) {
            float hitRate = static_cast<float>(cachedTokens) / static_cast<float>(promptTokens) * 100.0f;
            if (log) {
                log->info("[Cache] role={} | model={} | prompt={} | completion={} | total={} | cached={} | "
                          "hit_rate={:.1f}%",
                  role, model, promptTokens, completionTokens, totalTokens, cachedTokens, hitRate);
            } else {
                spdlog::info("[Cache] role={} | model={} | prompt={} | completion={} | total={} | cached={} | "
                             "hit_rate={:.1f}%",
                  role, model, promptTokens, completionTokens, totalTokens, cachedTokens, hitRate);
            }
        } else if (totalTokens > 0) {
            // 网关偶尔不返回 prompt 分解，用 total - completion 兜底，避免用量统计缺 prompt 数据
            promptTokens = std::max(0, totalTokens - completionTokens);
            const auto usageText = dumpJson(usage, false);
            if (log) {
                log->info("[Cache] role={} | model={} | prompt={} (no breakdown) | completion={} | total={} | "
                          "cached=N/A | hit_rate=N/A | usage={}",
                  role, model, promptTokens, completionTokens, totalTokens, usageText);
            } else {
                spdlog::info("[Cache] role={} | model={} | prompt={} (no breakdown) | completion={} | total={} | "
                             "cached=N/A | hit_rate=N/A | usage={}",
                  role, model, promptTokens, completionTokens, totalTokens, usageText);
            }
        }

        UsageStore::addUsageRecord(role, model, promptTokens, completionTokens, totalTokens, cachedTokens);

        Json::Value evt;
        evt["role"] = role;
        evt["model"] = model;
        if (sessionId.has_value()) {
            evt["groupId"] = *sessionId;
        }
        WebSocketManager::instance().broadcastEvent("usage_updated", evt);
    }
} // namespace insoulforge
