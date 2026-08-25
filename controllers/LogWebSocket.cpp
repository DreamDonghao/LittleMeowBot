/// @file LogWebSocket.cpp
/// @brief 运行日志 WebSocket 控制器 - 实现

#include <controllers/LogWebSocket.h>
#include <service/LogWebSocketManager.hpp>
#include <util/tool.h>
#include <spdlog/spdlog.h>
#include <sstream>

namespace LittleMeowBot {
    void LogWebSocket::handleNewConnection(
        const drogon::HttpRequestPtr &req,
        const drogon::WebSocketConnectionPtr &conn
    ) {
        LogWebSocketManager::instance().addConnection(conn);
        Json::Value welcome;
        welcome["type"] = "connected";
        welcome["message"] = "log websocket connected";
        Json::StreamWriterBuilder builder;
        conn->send(Json::writeString(builder, welcome));
    }

    void LogWebSocket::handleNewMessage(
        const drogon::WebSocketConnectionPtr &conn,
        std::string &&message,
        const drogon::WebSocketMessageType &type
    ) {
        if (type != drogon::WebSocketMessageType::Text) {
            return;
        }
        Json::Value msg;
        Json::CharReaderBuilder reader;
        std::string errs;
        std::istringstream stream(message);
        if (!Json::parseFromStream(reader, stream, &msg, &errs)) {
            spdlog::warn("日志WebSocket消息解析失败: {}", errs);
            return;
        }

        if (msg.get("action", "") == "subscribe") {
            LogSubscription sub;
            sub.all = !msg.isMember("groupId") || msg["groupId"].isNull()
                      || (msg["groupId"].isString() && msg["groupId"].asString() == "all");
            if (msg.isMember("groupId") && msg["groupId"].isString() && msg["groupId"].asString() == "system") {
                sub.all = false;
                sub.systemOnly = true;
            } else if (!sub.all) {
                // groupId 由前端以字符串形式发送（群号可能超过 JS 安全整数范围），
                // 不能直接 asUInt64()，否则 JSON 字符串会抛 LogicError
                sub.groupId = jsonToUInt64(msg["groupId"]);
            }
            if (msg.isMember("level") && msg["level"].isString() && msg["level"].asString() != "all") {
                sub.level = msg["level"].asString();
            }
            if (msg.isMember("keyword") && msg["keyword"].isString()) {
                sub.keyword = msg["keyword"].asString();
            }
            LogWebSocketManager::instance().updateSubscription(conn, std::move(sub));
        }
    }

    void LogWebSocket::handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) {
        LogWebSocketManager::instance().removeConnection(conn);
    }
}
