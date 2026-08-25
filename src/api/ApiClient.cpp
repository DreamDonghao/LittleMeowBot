/// @file ApiClient.cpp
/// @brief API 客户端 - 实现

#include <api/ApiClient.hpp>
#include <spdlog/spdlog.h>
#include <config/Config.hpp>
#include <storage/Database.hpp>
#include <service/WebSocketManager.hpp>
#include <util/HttpUtil.hpp>
#include <util/Logger.hpp>

namespace LittleMeowBot {
    namespace {
        /// @brief 构建模型请求体
        Json::Value buildModelReq(const Json::Value &messages, const std::string &model, float temperature, float top_p,
                                  int max_tokens) {
            Json::Value body;
            body["model"] = model;
            body["temperature"] = temperature;
            body["top_p"] = top_p;
            body["max_tokens"] = max_tokens;
            body["messages"] = messages;
            return body;
        }

        /// @brief 通用 API 请求函数
        drogon::Task<std::optional<std::string> > requestStr(
            const Json::Value &messages,
            const std::string &base_url,
            const std::string &path,
            const std::string &api_key,
            const std::string &model,
            float temperature,
            float top_p,
            int max_tokens,
            const std::string &role,
            std::optional<uint64_t> groupId) {
            const Json::Value body = buildModelReq(messages, model, temperature, top_p, max_tokens);
            const auto resp = co_await HttpUtil::send("[LLM]", base_url, path, drogon::Post, body, api_key, 90.0,
                                                      groupId);
            if (!resp) {
                co_return std::nullopt;
            }
            const auto json = (*resp)->getJsonObject();

            if ((*resp)->getStatusCode() != drogon::k200OK || !json || !json->isMember("choices")) {
                if (groupId) {
                    Logger::group(*groupId).error("[LLM] 请求出错: status={}",
                                                  static_cast<int>((*resp)->getStatusCode()));
                } else {
                    spdlog::error("[LLM] 请求出错: status={}", static_cast<int>((*resp)->getStatusCode()));
                }
                co_return std::nullopt;
            }

            ApiClient::logUsage(*json, model, role, groupId);

            const auto &choices = (*json)["choices"];
            if (!choices.isArray() || choices.empty()) {
                if (groupId) {
                    Logger::group(*groupId).error("LLM 返回格式错误: choices 不是数组或为空");
                } else {
                    spdlog::error("LLM 返回格式错误: choices 不是数组或为空");
                }
                co_return std::nullopt;
            }

            co_return choices[0]["message"]["content"].asString();
        }
    }

    drogon::Task<std::optional<std::string> > ApiClient::requestLLM(
        const Json::Value &messages,
        const float temperature,
        const float top_p,
        const int max_tokens,
        const std::string &role,
        const std::optional<uint64_t> groupId) {
        const auto &config = Config::instance();
        co_return co_await requestStr(
            messages,
            config.executor.baseUrl,
            config.executor.path,
            config.executor.apiKey,
            config.executor.model,
            temperature,
            top_p,
            max_tokens,
            role,
            groupId
        );
    }

    void ApiClient::logUsage(const Json::Value &responseJson, const std::string &model, const std::string &role,
                             const std::optional<uint64_t> groupId) {
        if (!responseJson.isMember("usage")) return;
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

        const auto log = groupId.has_value() ? std::optional<GroupLogger>(Logger::group(*groupId)) : std::nullopt;
        if (promptTokens > 0) {
            float hitRate = static_cast<float>(cachedTokens) / static_cast<float>(promptTokens) * 100.0f;
            if (log) {
                log->info(
                    "[Cache] role={} | model={} | prompt={} | completion={} | total={} | cached={} | hit_rate={:.1f}%",
                    role, model, promptTokens, completionTokens, totalTokens, cachedTokens, hitRate);
            } else {
                spdlog::info(
                    "[Cache] role={} | model={} | prompt={} | completion={} | total={} | cached={} | hit_rate={:.1f}%",
                    role, model, promptTokens, completionTokens, totalTokens, cachedTokens, hitRate);
            }
        } else if (totalTokens > 0) {
            // 网关偶尔不返回 prompt 分解，用 total - completion 兜底，避免用量统计缺 prompt 数据
            promptTokens = std::max(0, totalTokens - completionTokens);
            Json::StreamWriterBuilder compactWriter;
            compactWriter["indentation"] = "";
            const auto usageText = Json::writeString(compactWriter, usage);
            if (log) {
                log->info(
                    "[Cache] role={} | model={} | prompt={} (no breakdown) | completion={} | total={} | cached=N/A | hit_rate=N/A | usage={}",
                    role, model, promptTokens, completionTokens, totalTokens, usageText);
            } else {
                spdlog::info(
                    "[Cache] role={} | model={} | prompt={} (no breakdown) | completion={} | total={} | cached=N/A | hit_rate=N/A | usage={}",
                    role, model, promptTokens, completionTokens, totalTokens, usageText);
            }
        }

        Database::instance().addUsageRecord(
            role, model, promptTokens, completionTokens, totalTokens, cachedTokens);

        Json::Value evt;
        evt["role"] = role;
        evt["model"] = model;
        if (groupId.has_value()) {
            evt["groupId"] = static_cast<Json::UInt64>(*groupId);
        }
        WebSocketManager::instance().broadcastEvent("usage_updated", evt);
    }
}