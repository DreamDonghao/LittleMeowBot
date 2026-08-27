#include "AdminWebSocket.h"
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

    // 处理订阅请求
    if (msg.isMember("action")) {
        if (std::string action = msg["action"].asString(); action == "subscribe" && msg.isMember("sessionId")) {
            uint64_t sessionId = msg["groupId"].asUInt64();
            wsMgr.subscribeSession(conn, sessionId);

            // 发送确认
            Json::Value resp;
            resp["type"] = "subscribed";
            resp["groupId"] = sessionId;
            Json::StreamWriterBuilder builder;
            conn->send(Json::writeString(builder, resp));
        } else if (action == "unsubscribe" && msg.isMember("sessionId")) {
            uint64_t sessionId = msg["groupId"].asUInt64();
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
