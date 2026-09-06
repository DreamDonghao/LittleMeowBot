#include <agent/runtime/AgentSystem.hpp>
#include <atomic>
#include <config/Config.hpp>
#include <controllers/CommandHandler.hpp>
#include <controllers/ProcessQQMessages.hpp>
#include <ctime>
#include <model/QQMessage.hpp>
#include <service/ChatRecordManager.hpp>
#include <service/MemoryManager.hpp>
#include <service/MemoryService.hpp>
#include <service/MessageService.hpp>
#include <service/OneBotClient.hpp>
#include <service/SessionConfigManager.hpp>
#include <service/WebSocketManager.hpp>
#include <spdlog/spdlog.h>
#include <storage/SessionStore.hpp>
#include <util/CommonUtil.hpp>
#include <util/Logger.hpp>

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

    /// @brief 是否为拍一拍 notice 事件（OneBot: notice_type=notify, sub_type=poke；user_id=戳人者，target_id=被戳者）
    bool isPokeNotice(const json &body) {
        return getStr(body, "notice_type") == "notify" && getStr(body, "sub_type") == "poke" &&
               getUInt(body, "user_id", 0) != 0 && getUInt(body, "target_id", 0) != 0;
    }

    /// @brief 解析拍一拍参与者的昵称：notice 事件不带昵称，先查消息累积的映射表，未知时实时查资料补齐
    Task<std::string> resolvePokeName(const uint64_t qq, const uint64_t sessionId) {
        if (std::string name = QQMessage::getQQName(qq); name != "未知") {
            co_return name;
        }
        const auto resp = co_await OneBotClient::getStrangerInfo(qq, sessionId);
        std::string name = getStr(atOrNull(resp, "data"), "nickname");
        if (name.empty()) {
            name = "未知";
        }
        co_return name;
    }

    /// @brief 处理拍一拍 notice 事件（群聊带 group_id；私聊仅双方，即戳机器人）
    ///        - 机器人自己拍的：send_poke 工具已记入聊天记录，忽略
    ///        - 戳其他人的拍一拍：仅记入群聊天记录并推送 WebSocket，不触发回复
    ///        - 戳机器人的拍一拍：合成为真实戳者发送的文本消息事件，按普通消息走完整管线（Router 正常决策）
    /// @return 需要走消息管线的合成事件；仅记录或忽略时返回 null
    Task<json> handlePokeNotice(json notice) {
        const auto &config = Config::instance();
        const uint64_t pokerId = getUInt(notice, "user_id", 0);
        const uint64_t targetId = getUInt(notice, "target_id", 0);
        const uint64_t groupId = getUInt(notice, "group_id", 0);
        if (pokerId == config.selfQQNumber) {
            co_return json(); // 机器人自己拍的已由 send_poke 工具记录
        }

        // 会话：群拍一拍记入群会话；私聊拍一拍（戳机器人）记入对应私聊会话
        const uint64_t sessionId = groupId != 0 ? groupId : pokerId | QQMessage::kPrivateSessionFlag;
        const std::string pokerName = co_await resolvePokeName(pokerId, sessionId);
        const std::string text =
          fmt::format("[拍一拍：{}({})]", co_await resolvePokeName(targetId, sessionId), targetId);

        if (targetId != config.selfQQNumber) {
            if (groupId == 0) {
                co_return json(); // 私聊仅双方，理论上不存在，防御性忽略
            }
            // 其他人拍其他人：只记入聊天记录，不触发回复（拍一拍记录与 send_poke 一致，不带 message_id）
            json recordJson;
            recordJson["time"] = currentDateTime();
            recordJson["sender"]["name"] = pokerName;
            recordJson["sender"]["qq"] = std::to_string(pokerId);
            recordJson["text"] = text;
            recordJson["reply_to"] = nullptr;
            const ChatRecordManager chatRecords(groupId);
            chatRecords.addUserRecord(dumpJson(recordJson));
            WebSocketManager::instance().pushMessage(groupId, "user", text);
            co_return json();
        }

        // 戳的是机器人：合成戳者发出的消息事件（合成 ID 用远离真实 ID 的固定区段，与 TaskScheduler 的 9000000000
        // 区段错开）
        static std::atomic<int64_t> s_syntheticMsgId{0};
        json event;
        event["post_type"] = "message";
        event["self_id"] = config.selfQQNumber;
        event["time"] = static_cast<int64_t>(std::time(nullptr));
        event["message_id"] = fmt::to_string(9100000000LL + s_syntheticMsgId.fetch_add(1));
        event["raw_message"] = text;
        event["sender"]["user_id"] = pokerId;
        event["sender"]["nickname"] = pokerName;
        if (groupId != 0) {
            event["message_type"] = "group";
            event["group_id"] = groupId;
        } else {
            event["message_type"] = "private";
            event["user_id"] = pokerId;
        }
        json item;
        item["type"] = "text";
        item["data"]["text"] = text;
        event["message"].push_back(item);
        co_return event;
    }
} // namespace

Task<> ProcessQQMessages::receiveMessages(
  const HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) {
    auto body = parseJsonBody(req);
    if (!body) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("Invalid JSON or not an object");
        callback(resp);
        co_return;
    }
    // 返回响应（拍一拍等事件的处理放在应答之后，昵称补齐等异步操作不阻塞上报方）
    json respJson;
    respJson["status"] = "ok";
    callback(jsonResponse(respJson));

    // 检查 post_type：message 直通；拍一拍 notice 转换/记录；其余事件忽略
    if (getStr(*body, "post_type") != "message") {
        if (!isPokeNotice(*body)) {
            co_return;
        }
        // 拍一拍不会成为管理命令；禁用会话无需解析昵称、写入聊天记录或合成消息。
        const uint64_t groupId = getUInt(*body, "group_id", 0);
        const uint64_t pokerId = getUInt(*body, "user_id", 0);
        const uint64_t sessionId = groupId != 0 ? groupId : pokerId | QQMessage::kPrivateSessionFlag;
        if (!SessionStore::isSessionEnabled(sessionId)) {
            co_return;
        }
        *body = co_await handlePokeNotice(std::move(*body));
        if (body->is_null()) {
            co_return;
        }
    }

    if (!AgentSystem::instance().isRunning()) {
        co_return;
    }

    QQMessage qqMessage(std::move(*body));
    const uint64_t sessionId = qqMessage.getSessionId(); ///< 会话 ID（群聊=群号；私聊=用户QQ号|私聊标志位）
    // 确保会话配置存在
    if (!SessionConfigManager::contains(sessionId)) {
        SessionConfigManager::addConfig(sessionId);
    }

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

    // 格式化消息（含图片识别 LLM 调用，开销大，放在启用检查之后避免禁用会话白跑）
    co_await qqMessage.formatMessage();

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
