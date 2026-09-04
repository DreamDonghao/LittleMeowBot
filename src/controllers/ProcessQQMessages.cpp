#include <agent/AgentSystem.hpp>
#include <controllers/ProcessQQMessages.hpp>
#include <controllers/CommandHandler.hpp>
#include <service/ChatRecordManager.hpp>
#include <service/MemoryManager.hpp>
#include <model/QQMessage.hpp>
#include <service/SessionConfigManager.hpp>
#include <service/MemoryService.hpp>
#include <service/MessageService.hpp>
#include <service/WebSocketManager.hpp>
#include <spdlog/spdlog.h>
#include <util/Logger.hpp>
#include <storage/SessionStore.hpp>

using namespace insoulforge;
using namespace drogon;

namespace {
    /// @brief 按会话类型分派消息发送（群聊走 /send_group_msg，私聊走 /send_private_msg）
    /// @param message 触发回复的原始消息
    /// @param content 回复内容
    /// @param chatRecords 聊天记录管理器
    Task<> sendReply(const QQMessage message, std::string content, const ChatRecordManager &chatRecords) {
        if (message.isPrivate()) {
            co_await MessageService::sendPrivateMsg(message.getUserId(), std::move(content), chatRecords);
        } else {
            co_await MessageService::sendGroupMsg(message.getGroupId(), std::move(content), chatRecords);
        }
    }
} // namespace

Task<> ProcessQQMessages::receiveMessages(const HttpRequestPtr req,
                                          std::function<void(const HttpResponsePtr &)> callback) {
    const auto body = parseJsonBody(req);
    if (!body) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("Invalid JSON or not an object");
        callback(resp);
        co_return;
    }
    // 检查 post_type 字段
    if (getStr(*body, "post_type") != "message") {
        json resp;
        resp["status"] = "ok";
        callback(jsonResponse(resp));
        co_return;
    }
    // 返回响应
    json respJson;
    respJson["status"] = "ok";
    callback(jsonResponse(respJson));

    if (!AgentSystem::instance().isRunning()) {
        co_return;
    }

    QQMessage qqMessage(*body);
    const uint64_t sessionId = qqMessage.getSessionId(); ///< 会话 ID（群聊=群号；私聊=用户QQ号|私聊标志位）
    // 确保会话配置存在
    if (!SessionConfigManager::contains(sessionId)) {
        SessionConfigManager::addConfig(sessionId);
    }
    // 格式化消息
    co_await qqMessage.formatMessage();

    // 检查是否是命令消息（群聊需 @ 且以 / 开头，私聊直接以 / 开头）- 不受启用状态影响
    const auto log = Logger::session(sessionId);

    if (isCommand(qqMessage)) {
        log.info("收到命令消息: {}", qqMessage.getRawMessage());

        ChatRecordManager chatRecords(sessionId);

        std::string cmdResponse = co_await handleCommand(qqMessage);
        co_await sendReply(std::move(qqMessage), std::move(cmdResponse), chatRecords);
        co_return;
    }

    // 只处理启用的会话（从数据库读取）
    if (!SessionStore::isSessionEnabled(sessionId)) {
        co_return;
    }

    // 创建聊天记录和记忆管理器
    ChatRecordManager chatRecords(sessionId);
    MemoryManager memory(sessionId);

    auto &webSocketManager = WebSocketManager::instance();

    // 记录新的聊天消息
    if (qqMessage.getSelfQQNumber() == qqMessage.getSenderQQNumber()) {
        chatRecords.addAssistantRecord(qqMessage.getFormatMessage());
        webSocketManager.pushMessage(sessionId, "assistant", qqMessage.getFormatMessage());
    } else {
        chatRecords.addUserRecord(qqMessage.getFormatMessage());
        webSocketManager.pushMessage(sessionId, "user", qqMessage.getFormatMessage());
    }

    // 处理消息回复 - 使用多层代理架构
    auto &agentSystem = AgentSystem::instance();

    // 使用二层代理处理消息（顶层兜底，任何异常不让其逃逸到框架层）
    try {
        if (auto result = co_await agentSystem.process(chatRecords, memory, qqMessage);
            result && !result->empty() && agentSystem.isRunning()) {
            log.info("多层代理决定回复");
            // 表情包已由 send_sticker 工具直接发出并记入聊天记录，这里只发送文字回复
            co_await sendReply(qqMessage, std::move(result.value()), chatRecords);
        } else {
            log.info("多层代理决定不回复");
        }
    } catch (const std::exception &e) {
        log.error("消息处理异常: {}", e.what());
    }

    // 更新统计
    SessionConfigManager::incrementMessageCount(sessionId, qqMessage.getFormatMessage().size());
    auto [allMesCount, allCharCount] = SessionConfigManager::getConfig(sessionId);
    log.info("会话统计数据: 接收总消息数{}条,接收总字符(字节)数{}个", allMesCount, allCharCount);

    // 记忆提取与窗口滑动 - 窗口超限时触发（失败自愈：下条消息重试）
    try {
        co_await MemoryService::appendAndMergeMemory(sessionId);
    } catch (const std::exception &e) {
        log.error("记忆提取异常: {}", e.what());
    }
}
