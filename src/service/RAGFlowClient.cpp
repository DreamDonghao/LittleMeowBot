/// @file RAGFlowClient.cpp
/// @brief RAGFlow 知识库检索客户端 - 实现

#include <service/RAGFlowClient.hpp>
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <config/Config.hpp>

namespace LittleMeowBot {
    namespace {
        /// @brief 解析检索结果
        /// @param json RAGFlow API 返回的 JSON
        /// @return 格式化后的检索结果文本
        [[nodiscard]] std::string parseSearchResult(const Json::Value& json);
    }

    drogon::Task<std::optional<std::string>> RAGFlowClient::searchKnowledge(
        const std::string& question,
        int topK){
        const auto& kbConfig = Config::instance().knowledgeBase;

        // 检查是否启用 RAGFlow
        if (!kbConfig.enabled) {
            spdlog::debug("RAGFlow 未启用，跳过知识库检索");
            co_return std::nullopt;
        }

        const auto& apiKey = kbConfig.apiKey;
        const auto& baseUrl = kbConfig.baseUrl;
        const auto& knowledgeDatasetId = kbConfig.knowledgeDatasetId;

        if (knowledgeDatasetId.empty()) {
            spdlog::error("RAGFlow: 知识库ID未配置");
            co_return std::nullopt;
        }

        const auto client = drogon::HttpClient::newHttpClient(baseUrl);

        Json::Value body;
        body["dataset_ids"].append(knowledgeDatasetId);
        body["question"] = question;
        body["top_k"] = topK;

        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath("/api/v1/retrieval");
        req->addHeader("Authorization", "Bearer " + apiKey);

        drogon::HttpResponsePtr resp;
        try {
            resp = co_await client->sendRequestCoro(req, 30.0);
        } catch (const std::exception& e) {
            spdlog::error("RAGFlow 知识库检索网络异常: {}", e.what());
            co_return std::nullopt;
        }

        if (resp->getStatusCode() != drogon::k200OK) {
            spdlog::error("RAGFlow 请求失败: status={}", static_cast<int>(resp->getStatusCode()));
            co_return std::nullopt;
        }

        const auto json = resp->getJsonObject();
        if (!json) {
            spdlog::error("RAGFlow 响应解析失败");
            co_return std::nullopt;
        }

        std::string result = parseSearchResult(*json);
        if (result.empty()) {
            co_return std::nullopt;
        }

        co_return result;
    }

    drogon::Task<std::optional<std::string>> RAGFlowClient::searchMemory(
        const std::string& question,
        int topK){
        const auto& kbConfig = Config::instance().knowledgeBase;

        // 检查是否启用 RAGFlow
        if (!kbConfig.enabled) {
            spdlog::debug("RAGFlow 未启用，跳过记忆库检索");
            co_return std::nullopt;
        }

        const auto& apiKey = kbConfig.apiKey;
        const auto& baseUrl = kbConfig.baseUrl;
        const auto& memoryDatasetId = kbConfig.memoryDatasetId;

        if (memoryDatasetId.empty()) {
            spdlog::warn("RAGFlow: 记忆库ID未配置");
            co_return std::nullopt;
        }

        const auto client = drogon::HttpClient::newHttpClient(baseUrl);

        Json::Value body;
        body["dataset_ids"].append(memoryDatasetId);
        body["question"] = question;
        body["top_k"] = topK;

        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath("/api/v1/retrieval");
        req->addHeader("Authorization", "Bearer " + apiKey);

        drogon::HttpResponsePtr resp;
        try {
            resp = co_await client->sendRequestCoro(req, 30.0);
        } catch (const std::exception& e) {
            spdlog::error("RAGFlow 记忆库检索网络异常: {}", e.what());
            co_return std::nullopt;
        }

        if (resp->getStatusCode() != drogon::k200OK) {
            spdlog::error("RAGFlow 记忆库请求失败: status={}",static_cast<int>(resp->getStatusCode()));
            co_return std::nullopt;
        }

        const auto json = resp->getJsonObject();
        if (!json) {
            co_return std::nullopt;
        }

        co_return parseSearchResult(*json);
    }

    drogon::Task<bool> RAGFlowClient::addMemory(const std::string& content){
        const auto& kbConfig = Config::instance().knowledgeBase;

        // 检查是否启用 RAGFlow
        if (!kbConfig.enabled) {
            spdlog::debug("RAGFlow 未启用，跳过添加记忆");
            co_return false;
        }

        const auto& apiKey = kbConfig.apiKey;
        const auto& baseUrl = kbConfig.baseUrl;
        const auto& memoryDatasetId = kbConfig.memoryDatasetId;
        const auto& memoryDocumentId = kbConfig.memoryDocumentId;

        if (memoryDatasetId.empty()) {
            spdlog::warn("RAGFlow: 记忆库ID未配置，无法添加记忆");
            co_return false;
        }

        if (memoryDocumentId.empty()) {
            spdlog::warn("RAGFlow: 记忆文档ID未配置，请在管理后台配置");
            co_return false;
        }

        const auto client = drogon::HttpClient::newHttpClient(baseUrl);

        Json::Value body;
        body["content"] = content;

        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath(fmt::format("/api/v1/datasets/{}/documents/{}/chunks",
                                 memoryDatasetId, memoryDocumentId));
        req->addHeader("Authorization", "Bearer " + apiKey);

        drogon::HttpResponsePtr resp;
        try {
            resp = co_await client->sendRequestCoro(req, 30.0);
        } catch (const std::exception& e) {
            spdlog::error("RAGFlow 添加记忆网络异常: {}", e.what());
            co_return false;
        }
        if (resp->getStatusCode() != drogon::k200OK) {
            spdlog::error("RAGFlow 添加记忆失败: status={}", static_cast<int>(resp->getStatusCode()));
            co_return false;
        }

        const auto respJson = resp->getJsonObject();
        if (respJson && respJson->isMember("code")) {
            int code = (*respJson)["code"].asInt();
            if (code != 0) {
                std::string msg = respJson->get("message", "").asString();
                spdlog::error("RAGFlow 添加记忆失败: code={} message={}", code, msg);
                co_return false;
            }
        }

        spdlog::info("RAGFlow 记忆已添加: {} 字符", content.size());
        co_return true;
    }

    namespace {
        std::string parseSearchResult(const Json::Value& json){
        if (!json.isMember("data") || !json["data"].isMember("chunks")) {
            spdlog::warn("RAGFlow 返回格式异常");
            return "";
        }

        const auto& chunks = json["data"]["chunks"];
        if (!chunks.isArray() || chunks.empty()) {
            return "未找到相关信息";
        }

        std::string result;
        int count = 0;

        for (const auto& chunk : chunks) {
            if (count >= 3) break;

            if (chunk.isMember("content")) {
                std::string content = chunk["content"].asString();

                if (chunk.isMember("similarity")) {
                    if (const float similarity = chunk["similarity"].asFloat(); similarity < 0.3f) continue;
                }

                result += content + "\n";
                count++;
            }
        }

        return result;
    }
    }
}