#include "AdminWebSocket.h"
#include <util/tool.h>
#include <spdlog/spdlog.h>

using namespace LittleMeowBot;
using namespace drogon;

void AdminWebSocket::handleNewConnection(const HttpRequestPtr &req, const WebSocketConnectionPtr &conn) {
    WebSocketManager::instance().addConnection(conn);

    // 发送欢迎消息
    Json::Value welcome;
    welcome["type"] = "connected";
    welcome["message"] = "WebSocket连接成功";

    const Json::StreamWriterBuilder builder;
    conn->send(Json::writeString(builder, welcome));
}

void AdminWebSocket::handleNewMessage(
    const WebSocketConnectionPtr &conn,
    std::string &&message,
    const WebSocketMessageType &type
) {
    if (type != WebSocketMessageType::Text) {
        return;
    }

    // 解析客户端消息
    Json::Value msg;
    Json::CharReaderBuilder reader;
    std::string errs;
    std::istringstream stream(message);

    if (!Json::parseFromStream(reader, stream, &msg, &errs)) {
        spdlog::warn("WebSocket消息解析失败: {}", errs);
        return;
    }

    auto &wsMgr = WebSocketManager::instance();

    // 处理订阅请求（线上 JSON 字段沿用 "groupId"，字符串形式可承载大数，内部语义为 sessionId）
    if (msg.isMember("action")) {
        if (std::string action = msg["action"].asString(); action == "subscribe" && msg.isMember("groupId")) {
            const uint64_t sessionId = parseUInt64(msg["groupId"].asString());
            wsMgr.subscribeSession(conn, sessionId);

            // 发送确认
            Json::Value resp;
            resp["type"] = "subscribed";
            resp["groupId"] = sessionId;
            Json::StreamWriterBuilder builder;
            conn->send(Json::writeString(builder, resp));
        } else if (action == "unsubscribe" && msg.isMember("groupId")) {
            const uint64_t sessionId = parseUInt64(msg["groupId"].asString());
            wsMgr.unsubscribeSession(conn, sessionId);

            Json::Value resp;
            resp["type"] = "unsubscribed";
            resp["groupId"] = sessionId;
            Json::StreamWriterBuilder builder;
            conn->send(Json::writeString(builder, resp));
        }
    }
}

void AdminWebSocket::handleConnectionClosed(const WebSocketConnectionPtr &conn) {
    WebSocketManager::instance().removeConnection(conn);
}
