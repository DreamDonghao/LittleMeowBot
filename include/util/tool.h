/// @file tool.h
/// @brief 通用工具函数
/// @author donghao
/// @date 2026-04-02
/// @details 提供通用工具函数：
///          - 随机数：randomBoolWithProbability()
///          - JSON 解析：parseJson()
///          - 字符串处理：split(), fillEmptyImages()
///          - 时间获取：currentDateTime()
///          - LLM 请求：requestStr(), requestGemini()

#pragma once
#include <drogon/drogon.h>
#include <random>
#include <ranges>

/// @brief 根据概率获取布尔值
inline bool randomBoolWithProbability(const double probability){
    if (probability <= 0.0)
        return false;
    if (probability >= 1.0)
        return true;

    thread_local std::random_device rd;
    thread_local std::mt19937 gen(rd());
    thread_local std::uniform_real_distribution<> dis(0.0, 1.0);

    return dis(gen) < probability;
}

inline Json::Value parseJson(const std::string& jsonStr){
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string errs;

    const char* begin = jsonStr.data();

    if (const char* end = begin + jsonStr.size(); !reader->parse(begin, end, &root, &errs)) {
        // 出错时打印并返回空对象
        std::cerr << "JSON解析失败: " << errs << "\n";
        return Json::Value{};
    }
    return root;
}


inline std::string fillEmptyImages(std::string s, const std::string& images){
    constexpr std::string_view kTag = "[图片：";
    size_t pos = 0;

    while ((pos = s.find(kTag, pos)) != std::string::npos) {
        const size_t endPos = s.find(']', pos);
        if (endPos == std::string::npos)
            break;

        // [图片：] —— 冒号后没有内容
        if (endPos == pos + kTag.size()) {
            s.insert(endPos, images);
            pos = endPos + images.size() + 1;
        } else {
            pos = endPos + 1;
        }
    }

    return s;
}

inline Json::Value buildModelReq(const Json::Value& messages, const std::string& model, const float temperature,
                                 const float top_p, const int max_tokens){
    Json::Value body;
    body["model"] = model;
    body["temperature"] = temperature;
    body["top_p"] = top_p;
    body["max_tokens"] = max_tokens;
    body["messages"] = messages;
    return body;
}

#include <chrono>
#include <ctime>
#include <fmt/chrono.h>
#include <fmt/core.h>

inline std::string currentDateTime(){
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    return fmt::format("{:%Y-%m-%d %H:%M:%S}", tm);
}


inline std::vector<std::string> split(const std::string& str, const std::string& delim){
    std::vector<std::string> result;
    if (delim.empty()) {
        result.push_back(str);
        return result;
    }
    std::size_t pos = 0;
    while (true) {
        const auto next = str.find(delim, pos);
        if (next == std::string::npos) {
            result.emplace_back(str.substr(pos));
            break;
        }
        result.emplace_back(str.substr(pos, next - pos));
        pos = next + delim.size();
    }

    return result;
}

inline drogon::Task<std::optional<std::string>> requestStr(
    const Json::Value& messages,
    const std::string& base_url, const std::string& path,
    const std::string& api_key, const std::string& model,
    const float temperature, const float top_p, const int max_tokens){
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
        LOG_ERROR << "Api 请求出错: status=" << resp->getStatusCode();
        co_return std::nullopt;
    }
    const Json::Value& choices = (*json)["choices"];
    if (!choices.isArray() || choices.empty()) {
        LOG_ERROR << "Api 返回格式错误";
        co_return std::nullopt;
    }
    const std::string content = choices[0]["message"]["content"].asString();
    co_return content;
}

inline drogon::Task<std::optional<std::string>> requestGemini(
    const Json::Value& messages,
    const std::string& base_url, // https://generativelanguage.googleapis.com
    const std::string& model, // gemini-pro / gemini-1.5-pro 等
    const std::string& api_key,
    const float temperature,
    const float top_p,
    const int max_tokens){
    // 1. 构造 URL（Gemini 把 key 放 query）
    const std::string path =
        "/v1beta/models/" + model + ":generateContent?key=" + api_key;

    const auto client = drogon::HttpClient::newHttpClient(base_url);

    // 2. 将 OpenAI messages 转为 Gemini contents
    Json::Value body;
    auto& contents = body["contents"];

    for (Json::ArrayIndex i = 0; i < messages.size(); ++i) {
        const auto& msg = messages[i];
        const std::string role = msg.get("role", "").asString();
        const std::string text = msg.get("content", "").asString();

        Json::Value item;
        // Gemini 不强制 role，但支持 user / model
        if (role == "assistant")
            item["role"] = "model";
        else
            item["role"] = "user";

        item["parts"][0]["text"] = text;
        contents.append(item);
    }

    // 3. 生成参数
    body["generationConfig"]["temperature"] = temperature;
    body["generationConfig"]["topP"] = top_p;
    body["generationConfig"]["maxOutputTokens"] = max_tokens;

    // 4. 发请求
    const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
    req->setMethod(drogon::Post);
    req->setPath(path);
    req->addHeader("Content-Type", "application/json");

    const auto resp = co_await client->sendRequestCoro(req);

    if (!resp || resp->getStatusCode() != drogon::k200OK) {
        LOG_ERROR << "Gemini API 请求失败";
        co_return std::nullopt;
    }

    const auto json = resp->getJsonObject();
    if (!json || !json->isMember("candidates")) {
        LOG_ERROR << "Gemini 返回 JSON 解析失败";
        co_return std::nullopt;
    }

    // 5. 解析返回
    const Json::Value& candidates = (*json)["candidates"];
    if (!candidates.isArray() || candidates.empty()) {
        LOG_ERROR << "Gemini 返回结构异常";
        co_return std::nullopt;
    }

    const auto& parts =
        candidates[0]["content"]["parts"];

    if (!parts.isArray() || parts.empty()) {
        LOG_ERROR << "Gemini 无有效文本";
        co_return std::nullopt;
    }

    const std::string content = parts[0]["text"].asString();
    co_return content;
}
