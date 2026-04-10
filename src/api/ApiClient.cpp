/// @file ApiClient.cpp
/// @brief API 客户端 - 实现

#include <api/ApiClient.hpp>
#include <spdlog/spdlog.h>
#include <drogon/HttpClient.h>
#include <config/Config.hpp>

namespace LittleMeowBot {
    ApiClient& ApiClient::instance(){
        static ApiClient client;
        return client;
    }

    void ApiClient::registerTool(const Tool& tool) const{
        ToolRegistry::instance().registerTool(tool, ToolCategory::ACTION);
    }

    Json::Value ApiClient::getToolsJson() const{
        return ToolRegistry::instance().getAllTools();
    }

    drogon::Task<std::string> ApiClient::executeTool(const std::string& name, const Json::Value& args) const{
        co_return co_await ToolRegistry::instance().executeTool(name, args);
    }

    drogon::Task<std::string> ApiClient::fetchWeather(const std::string& city){
        const auto client = drogon::HttpClient::newHttpClient("https://uapis.cn");
        const auto req = drogon::HttpRequest::newHttpRequest();
        req->setMethod(drogon::Get);
        req->setPath("/api/v1/misc/weather?city=" + city);
        req->addHeader("Accept", "application/json");

        const auto resp = co_await client->sendRequestCoro(req);
        if (resp->getStatusCode() != drogon::k200OK) {
            co_return "天气查询失败";
        }

        const auto json = resp->getJsonObject();
        if (!json) {
            co_return "天气数据解析失败";
        }

        std::string result;
        if (json->isMember("city")) {
            result = (*json)["city"].asString();
        }
        if (json->isMember("weather")) {
            result += " " + (*json)["weather"].asString();
        }
        if (json->isMember("temperature")) {
            result += " " + std::to_string(static_cast<int>((*json)["temperature"].asDouble())) + "°C";
        }
        if (json->isMember("humidity")) {
            result += " 湿度" + std::to_string((*json)["humidity"].asInt()) + "%";
        }
        if (json->isMember("wind_direction") && json->isMember("wind_power")) {
            result += " " + (*json)["wind_direction"].asString() + (*json)["wind_power"].asString();
        }

        co_return result.empty() ? "天气信息获取失败" : result;
    }

    drogon::Task<std::string> ApiClient::searchWeb(const std::string& query){
        const auto client = drogon::HttpClient::newHttpClient("https://uapis.cn");

        Json::Value body;
        body["query"] = query;

        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath("/api/v1/search/aggregate");
        req->addHeader("Accept", "application/json");

        try {
            const auto resp = co_await client->sendRequestCoro(req);
            if (resp->getStatusCode() != drogon::k200OK) {
                co_return "搜索失败";
            }

            const auto json = resp->getJsonObject();
            if (!json) {
                co_return "搜索结果解析失败";
            }

            std::string result;
            if (json->isMember("results") && (*json)["results"].isArray()) {
                const auto& results = (*json)["results"];
                int count = 0;
                for (const auto& item : results) {
                    if (count >= 3) break;
                    if (item.isMember("title")) {
                        result += std::to_string(count + 1) + ". " + item["title"].asString();
                        if (item.isMember("snippet")) {
                            result += "\n   " + item["snippet"].asString();
                        }
                        result += "\n";
                        count++;
                    }
                }
            }

            co_return result.empty() ? "未找到相关结果" : result;
        } catch (const std::exception& e) {
            spdlog::error("搜索异常: {}", e.what());
            co_return "搜索服务暂时不可用";
        }
    }

    drogon::Task<std::optional<std::string>> ApiClient::requestLLM(
        const Json::Value& messages,
        const float temperature,
        const float top_p,
        const int max_tokens) const{
        const auto& config = Config::instance();
        co_return co_await requestStr(
            messages,
            config.executor.baseUrl,
            config.executor.path,
            config.executor.apiKey,
            config.executor.model,
            temperature,
            top_p,
            max_tokens
        );
    }

    drogon::Task<std::optional<ApiResponse>> ApiClient::requestWithTools(
        const Json::Value& messages,
        const Json::Value& tools,
        const std::string& base_url,
        const std::string& api_key,
        const std::string& model,
        float temperature,
        float top_p,
        int max_tokens) const{
        const auto client = drogon::HttpClient::newHttpClient(base_url);
        Json::Value body = buildModelReq(messages, model, temperature, top_p, max_tokens);
        body["tools"] = tools;

        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath("/v1/chat/completions");
        req->addHeader("Authorization", "Bearer " + api_key);
        req->addHeader("Content-Type", "application/json");

        const auto resp = co_await client->sendRequestCoro(req);
        const auto json = resp->getJsonObject();

        if (resp->getStatusCode() != drogon::k200OK || !json || !json->isMember("choices")) {
            LOG_ERROR << "API请求出错: status=" << resp->getStatusCode();
            co_return std::nullopt;
        }

        const auto& message = (*json)["choices"][0]["message"];
        ApiResponse result;

        if (message.isMember("content") && !message["content"].isNull()) {
            result.content = message["content"].asString();
        }

        if (message.isMember("tool_calls")) {
            for (const auto& tc : message["tool_calls"]) {
                ToolCall call;
                call.id = tc["id"].asString();
                call.name = tc["function"]["name"].asString();
                call.arguments = tc["function"]["arguments"].asString();
                result.toolCalls.push_back(call);
            }
        }

        co_return result;
    }

    Json::Value ApiClient::buildModelReq(
        const Json::Value& messages,
        const std::string& model,
        float temperature,
        float top_p,
        int max_tokens){
        Json::Value body;
        body["model"] = model;
        body["temperature"] = temperature;
        body["top_p"] = top_p;
        body["max_tokens"] = max_tokens;
        body["messages"] = messages;
        return body;
    }

    drogon::Task<std::optional<std::string>> ApiClient::requestStr(
        const Json::Value& messages,
        const std::string& base_url,
        const std::string& path,
        const std::string& api_key,
        const std::string& model,
        float temperature,
        float top_p,
        int max_tokens){
        const auto client = drogon::HttpClient::newHttpClient(base_url);
        const Json::Value body = buildModelReq(messages, model, temperature, top_p, max_tokens);
        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath(path);
        req->addHeader("Authorization", "Bearer " + api_key);
        req->addHeader("Content-Type", "application/json");

        const auto resp = co_await client->sendRequestCoro(req);
        const auto json = resp->getJsonObject();

        if (resp->getStatusCode() != drogon::k200OK || !json || !json->isMember("choices")) {
            LOG_ERROR << "API 请求出错: status=" << resp->getStatusCode();
            co_return std::nullopt;
        }

        const auto& choices = (*json)["choices"];
        if (!choices.isArray() || choices.empty()) {
            LOG_ERROR << "API 返回格式错误: choices 不是数组或为空";
            co_return std::nullopt;
        }

        co_return choices[0]["message"]["content"].asString();
    }

    drogon::Task<std::optional<std::string>> ApiClient::requestAgent(
        Json::Value messages,
        const std::string& base_url,
        const std::string& api_key,
        const std::string& model,
        int maxIterations) const{
        auto tools = getToolsJson();
        if (tools.empty()) {
            LOG_ERROR << "未注册任何工具";
            co_return std::nullopt;
        }

        for (int i = 0; i < maxIterations; ++i) {
            auto response = co_await requestWithTools(
                messages, tools, base_url, api_key, model, 0.7f, 0.9f, 1024);

            if (!response) {
                co_return std::nullopt;
            }

            if (!response->hasToolCalls()) {
                co_return response->content;
            }

            spdlog::info("Agent执行工具调用，轮次{}", i + 1);

            Json::Value assistantMsg;
            assistantMsg["role"] = "assistant";
            assistantMsg["content"] = response->hasContent() ? response->content : "";
            Json::Value toolCallsJson;
            for (const auto& tc : response->toolCalls) {
                Json::Value tcJson;
                tcJson["id"] = tc.id;
                tcJson["type"] = "function";
                tcJson["function"]["name"] = tc.name;
                tcJson["function"]["arguments"] = tc.arguments;
                toolCallsJson.append(tcJson);
            }
            assistantMsg["tool_calls"] = toolCallsJson;
            messages.append(assistantMsg);

            for (const auto& tc : response->toolCalls) {
                Json::Value args;
                Json::Reader reader;
                reader.parse(tc.arguments, args);

                if (tc.name == "no_reply") {
                    co_return std::nullopt;
                }
                if (tc.name == "reply") {
                    if (args.isMember("content")) {
                        co_return args["content"].asString();
                    }
                    co_return std::nullopt;
                }

                std::string result;
                if (tc.name == "get_weather") {
                    result = co_await fetchWeather(args.isMember("city") ? args["city"].asString() : "");
                } else if (tc.name == "search_web") {
                    result = co_await searchWeb(args.isMember("query") ? args["query"].asString() : "");
                } else {
                    result = co_await executeTool(tc.name, args);
                }
                spdlog::info("工具 {} 返回: {}", tc.name, result);

                Json::Value toolMsg;
                toolMsg["role"] = "tool";
                toolMsg["tool_call_id"] = tc.id;
                toolMsg["content"] = result;
                messages.append(toolMsg);
            }
        }

        LOG_ERROR << "Agent达到最大迭代次数";
        co_return std::nullopt;
    }

    drogon::Task<std::optional<ReplyDecision>> ApiClient::requestReplyAgent(
        Json::Value messages,
        const std::string& base_url,
        const std::string& api_key,
        const std::string& model,
        int maxIterations) const{
        auto tools = getToolsJson();
        auto client = drogon::HttpClient::newHttpClient(base_url);

        for (int iter = 0; iter < maxIterations; ++iter) {
            Json::Value body = buildModelReq(messages, model, 0.7f, 0.9f, 512);
            body["tools"] = tools;

            auto req = drogon::HttpRequest::newHttpJsonRequest(body);
            req->setMethod(drogon::Post);
            req->setPath("/v1/chat/completions");
            req->addHeader("Authorization", "Bearer " + api_key);
            req->addHeader("Content-Type", "application/json");

            auto resp = co_await client->sendRequestCoro(req);
            auto json = resp->getJsonObject();

            if (resp->getStatusCode() != drogon::k200OK || !json || !json->isMember("choices")) {
                LOG_ERROR << "API请求出错: status=" << resp->getStatusCode();
                co_return std::nullopt;
            }

            const auto& message = (*json)["choices"][0]["message"];
            ReplyDecision decision;

            if (message.isMember("tool_calls") && !message["tool_calls"].empty()) {
                bool hasFinalDecision = false;
                Json::Value assistantMsg;
                assistantMsg["role"] = "assistant";
                assistantMsg["content"] = "";

                Json::Value toolCallsJson;
                for (const auto& tc : message["tool_calls"]) {
                    Json::Value tcJson;
                    tcJson["id"] = tc["id"].asString();
                    tcJson["type"] = "function";
                    tcJson["function"]["name"] = tc["function"]["name"].asString();
                    tcJson["function"]["arguments"] = tc["function"]["arguments"].asString();
                    toolCallsJson.append(tcJson);
                }
                assistantMsg["tool_calls"] = toolCallsJson;

                for (const auto& tc : message["tool_calls"]) {
                    std::string toolName = tc["function"]["name"].asString();
                    std::string toolId = tc["id"].asString();
                    std::string argsStr = tc["function"]["arguments"].asString();

                    spdlog::info("Agent调用工具: {}", toolName);

                    if (toolName == "no_reply") {
                        decision.shouldReply = false;
                        hasFinalDecision = true;
                    } else if (toolName == "reply") {
                        Json::Reader reader;
                        if (Json::Value args;
                            reader.parse(argsStr, args) && args.isMember("content")) {
                            decision.shouldReply = true;
                            decision.content = args["content"].asString();
                            hasFinalDecision = true;
                        }
                    } else {
                        Json::Value args;
                        Json::Reader reader;
                        reader.parse(argsStr, args);

                        std::string result;
                        if (toolName == "get_weather") {
                            result = co_await fetchWeather(args.isMember("city") ? args["city"].asString() : "");
                        } else if (toolName == "search_web") {
                            result = co_await searchWeb(args.isMember("query") ? args["query"].asString() : "");
                        } else {
                            result = co_await executeTool(toolName, args);
                        }
                        spdlog::info("工具 {} 返回: {}", toolName, result);

                        Json::Value toolMsg;
                        toolMsg["role"] = "tool";
                        toolMsg["tool_call_id"] = toolId;
                        toolMsg["content"] = result;
                        messages.append(toolMsg);
                    }
                }

                if (hasFinalDecision) {
                    co_return decision;
                }

                messages.append(assistantMsg);
                continue;
            }

            if (message.isMember("content") && !message["content"].isNull()) {
                decision.shouldReply = true;
                decision.content = message["content"].asString();
                co_return decision;
            }

            decision.shouldReply = false;
            co_return decision;
        }

        LOG_ERROR << "Agent达到最大迭代次数";
        co_return std::nullopt;
    }
}