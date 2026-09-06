/// @file EventNormalizationMiddleware.cpp
/// @brief OneBot 事件归一化中间件实现

#include <atomic>
#include <config/Config.hpp>
#include <ctime>
#include <event/DomainEvent.hpp>
#include <event/EventBus.hpp>
#include <fmt/format.h>
#include <message/MessageContext.hpp>
#include <message/middleware/EventNormalizationMiddleware.hpp>
#include <service/OneBotClient.hpp>
#include <storage/SessionStore.hpp>
#include <util/CommonUtil.hpp>

namespace insoulforge {
    namespace {
        /// @brief 判断事件是否为 OneBot 拍一拍通知
        /// @param event 待检查的 OneBot 事件
        /// @return 事件字段完整且类型为拍一拍时返回 true
        [[nodiscard]] bool isPokeNotice(const json &event) {
            return getStr(event, "notice_type") == "notify" && getStr(event, "sub_type") == "poke" &&
                   getUInt(event, "user_id", 0) != 0 && getUInt(event, "target_id", 0) != 0;
        }

        /// @brief 计算拍一拍事件所属的统一会话 ID
        /// @param event 已确认是拍一拍的 OneBot 事件
        /// @return 群会话 ID，或带私聊标志位的拍一拍发起者 QQ 号
        [[nodiscard]] uint64_t pokeSessionId(const json &event) {
            const uint64_t groupId = getUInt(event, "group_id", 0);
            const uint64_t pokerId = getUInt(event, "user_id", 0);
            return groupId != 0 ? groupId : pokerId | QQMessage::kPrivateSessionFlag;
        }

        /// @brief 获取拍一拍参与者的显示名称
        /// @param qq 参与者 QQ 号
        /// @param sessionId 用于请求日志关联的会话 ID
        /// @return 本地缓存昵称、OneBot 查询昵称，或回退值“未知”
        drogon::Task<std::string> resolvePokeName(const uint64_t qq, const uint64_t sessionId) {
            if (std::string name = QQMessage::getQQName(qq); name != "未知") {
                co_return name;
            }
            const auto response = co_await OneBotClient::getStrangerInfo(qq, sessionId);
            std::string name = getStr(atOrNull(response, "data"), "nickname");
            co_return name.empty() ? "未知" : name;
        }

        /// @brief 处理拍一拍事件，必要时合成为一条普通文本消息
        /// @param notice 已通过 isPokeNotice() 校验且会话已启用的事件
        /// @return 戳机器人时返回合成消息；已处理或需忽略时返回 null JSON
        drogon::Task<json> normalizePokeNotice(json notice) {
            const auto &config = Config::instance();
            const uint64_t pokerId = getUInt(notice, "user_id", 0);
            const uint64_t targetId = getUInt(notice, "target_id", 0);
            const uint64_t groupId = getUInt(notice, "group_id", 0);
            if (pokerId == config.selfQQNumber) {
                co_return json();
            }

            const uint64_t sessionId = groupId != 0 ? groupId : pokerId | QQMessage::kPrivateSessionFlag;
            const std::string pokerName = co_await resolvePokeName(pokerId, sessionId);
            const std::string text =
              fmt::format("[拍一拍：{}({})]", co_await resolvePokeName(targetId, sessionId), targetId);

            if (targetId != config.selfQQNumber) {
                if (groupId == 0) {
                    co_return json();
                }
                json record;
                record["time"] = currentDateTime();
                record["sender"]["name"] = pokerName;
                record["sender"]["qq"] = std::to_string(pokerId);
                record["text"] = text;
                record["reply_to"] = nullptr;
                const std::string recordContent = dumpJson(record);
                const ChatRecordManager chatRecords(groupId);
                chatRecords.addUserRecord(recordContent);
                co_await EventBus::instance().publish(MessageRecordedEvent{
                  .sessionId = groupId,
                  .messageId = 0,
                  .role = MessageRole::User,
                  .recordContent = recordContent,
                  .displayContent = text,
                });
                co_return json();
            }

            // 与 TaskScheduler 使用的 9000000000 区段隔离，避免召回与引用消息发生 ID 冲突。
            static std::atomic<int64_t> syntheticMessageId{0};
            json event;
            event["post_type"] = "message";
            event["self_id"] = config.selfQQNumber;
            event["time"] = static_cast<int64_t>(std::time(nullptr));
            event["message_id"] = fmt::to_string(9100000000LL + syntheticMessageId.fetch_add(1));
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
            event["message"].push_back({{"type", "text"}, {"data", {{"text", text}}}});
            co_return event;
        }
    } // namespace

    std::string_view EventNormalizationMiddleware::id() const noexcept { return "event_normalization"; }

    drogon::Task<MessageFlow> EventNormalizationMiddleware::handle(MessageContext &context) const {
        json &event = context.event();
        if (getStr(event, "post_type") == "message") {
            co_return MessageFlow::Continue;
        }
        if (!isPokeNotice(event) || !SessionStore::isSessionEnabled(pokeSessionId(event))) {
            co_return MessageFlow::Stop;
        }

        event = co_await normalizePokeNotice(std::move(event));
        co_return event.is_null() ? MessageFlow::Stop : MessageFlow::Continue;
    }
} // namespace insoulforge
