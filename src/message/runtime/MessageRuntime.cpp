/// @file MessageRuntime.cpp
/// @brief 默认消息运行时实现

#include <agent/runtime/AgentSystem.hpp>
#include <event/EventBus.hpp>
#include <message/runtime/MessageRuntime.hpp>
#include <model/QQMessage.hpp>
#include <service/ChatRecordManager.hpp>
#include <service/MemoryManager.hpp>
#include <service/MessageService.hpp>

namespace insoulforge {
    namespace {
        /// @brief 连接既有进程级服务的生产运行时
        class BuiltinMessageRuntime final : public MessageRuntime {
        public:
            [[nodiscard]] bool isAgentRunning() const override { return AgentSystem::instance().isRunning(); }

            drogon::Task<std::optional<std::string>> processAgent(
              ChatRecordManager &chatRecords, MemoryManager &memory, const QQMessage &message) const override {
                co_return co_await AgentSystem::instance().process(chatRecords, memory, message);
            }

            drogon::Task<> sendReply(
              const QQMessage &message, const ChatRecordManager &chatRecords, std::string content) const override {
                if (message.isPrivate()) {
                    co_await MessageService::sendPrivateMsg(message.getUserId(), std::move(content), chatRecords);
                } else {
                    co_await MessageService::sendGroupMsg(message.getGroupId(), std::move(content), chatRecords);
                }
            }

            drogon::Task<> publish(DomainEvent event) const override {
                co_await EventBus::instance().publish(std::move(event));
            }
        };
    } // namespace

    std::shared_ptr<const MessageRuntime> createBuiltinMessageRuntime() {
        return std::make_shared<BuiltinMessageRuntime>();
    }
} // namespace insoulforge
