/// @file HttpUtil.cpp
/// @brief HTTP 请求工具 - 实现

#include <util/HttpUtil.hpp>
#include <spdlog/spdlog.h>
#include <json/writer.h>
#include <string>
#include <utility>

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

    drogon::Task<std::optional<drogon::HttpResponsePtr> > send(
        const std::string_view tag,
        const std::string &baseUrl,
        const std::string &path,
        const drogon::HttpMethod method,
        const Json::Value &body,
        const std::string &bearerToken,
        const double timeout,
        std::optional<uint64_t> groupId) {
        const auto prefix = groupId.has_value()
                                ? fmt::format("[group_id={}] {}", *groupId, tag)
                                : std::string(tag);
        // 正常请求详情仅记录到 debug，避免每条消息多次请求刷屏
        spdlog::debug("{} [HTTP] {} {}{}", prefix, methodName(method), baseUrl, path);
        if (!body.isNull()) {
            spdlog::debug("{} [HTTP] body={}", prefix, truncate(serializeBody(body), kBodyLogMax));
        }
        if (!bearerToken.empty()) {
            spdlog::debug("{} [HTTP] Authorization: Bearer {}", prefix, maskToken(bearerToken));
        }

        // 失败时才附带完整地址与 body 打日志，确保凭日志即可定位问题
        const auto bodyLog = body.isNull() ? std::string{} : truncate(serializeBody(body), kBodyLogMax);

        drogon::HttpClientPtr client;
        try {
            client = drogon::HttpClient::newHttpClient(baseUrl);
        } catch (const std::exception &e) {
            spdlog::error("{} [HTTP] 创建客户端失败: {} ({} {}{}) body={}",
                          prefix, e.what(), methodName(method), baseUrl, path, bodyLog);
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

        drogon::HttpResponsePtr resp;
        try {
            resp = co_await client->sendRequestCoro(req, timeout);
        } catch (const std::exception &e) {
            spdlog::error("{} [HTTP] 请求异常: {} ({} {}{}) body={}",
                          prefix, e.what(), methodName(method), baseUrl, path, bodyLog);
            co_return std::nullopt;
        }

        // 非 2xx（如 DNS 解析失败、连接被拒等）同样把地址打出来，方便定位
        if (resp && resp->getStatusCode() >= drogon::k400BadRequest) {
            spdlog::warn("{} [HTTP] 响应异常: status={} ({} {}{})",
                         prefix, static_cast<int>(resp->getStatusCode()), methodName(method), baseUrl, path);
        }

        co_return resp;
    }
} // namespace LittleMeowBot::HttpUtil
