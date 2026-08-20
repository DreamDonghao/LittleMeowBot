/// @file HttpUtil.cpp
/// @brief HTTP 请求工具 - 实现

#include <util/HttpUtil.hpp>
#include <spdlog/spdlog.h>
#include <json/writer.h>

namespace LittleMeowBot::HttpUtil {
    namespace {
        constexpr size_t kBodyLogMax = 400; // 日志中请求体截断长度

        const char *methodName(const drogon::HttpMethod m) {
            switch (m) {
                case drogon::Get: return "GET";
                case drogon::Post: return "POST";
                case drogon::Put: return "PUT";
                case drogon::Delete: return "DELETE";
                case drogon::Options: return "OPTIONS";
                case drogon::Patch: return "PATCH";
                case drogon::Head: return "HEAD";
                default: return "?";
            }
        }

        std::string serializeBody(const Json::Value &body) {
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            writer["emitUTF8"] = true;
            return Json::writeString(writer, body);
        }

        std::string truncate(std::string s, const size_t max) {
            if (s.size() <= max) return s;
            return s.substr(0, max) + "…(截断)";
        }

        // 脱敏：只保留头尾各 4 位，避免 token 明文进日志
        std::string maskToken(const std::string_view token) {
            if (token.empty()) return "(空)";
            if (token.size() <= 8) return std::string(token.size(), '*');
            return std::string(token.substr(0, 4)) + "****" +
                   std::string(token.substr(token.size() - 4));
        }
    } // namespace

    drogon::Task<std::optional<drogon::HttpResponsePtr>> send(
        const std::string_view tag,
        const std::string &baseUrl,
        const std::string &path,
        const drogon::HttpMethod method,
        const Json::Value &body,
        const std::string &bearerToken,
        const double timeout) {
        // 请求前记录：方法与完整 URL（便于直接定位地址问题）
        spdlog::info("{} [HTTP] {} {}{}", tag, methodName(method), baseUrl, path);
        if (!body.isNull()) {
            spdlog::info("{} [HTTP] body={}", tag, truncate(serializeBody(body), kBodyLogMax));
        }
        if (!bearerToken.empty()) {
            spdlog::debug("{} [HTTP] Authorization: Bearer {}", tag, maskToken(bearerToken));
        }

        drogon::HttpClientPtr client;
        try {
            client = drogon::HttpClient::newHttpClient(baseUrl);
        } catch (const std::exception &e) {
            spdlog::error("{} [HTTP] 创建客户端失败: {} (baseUrl={})", tag, e.what(), baseUrl);
            co_return std::nullopt;
        }

        const auto req = body.isNull()
                             ? drogon::HttpRequest::newHttpRequest()
                             : drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(method);
        req->setPath(path);
        if (!bearerToken.empty()) {
            req->addHeader("Authorization", "Bearer " + bearerToken);
        }

        try {
            co_return co_await client->sendRequestCoro(req, timeout);
        } catch (const std::exception &e) {
            spdlog::error("{} [HTTP] 请求异常: {} ({} {}{})",
                          tag, e.what(), methodName(method), baseUrl, path);
            co_return std::nullopt;
        }
    }
} // namespace LittleMeowBot::HttpUtil