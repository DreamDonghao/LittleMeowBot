/// @file CommandHandler.cpp
/// @brief 命令处理器 - 实现

#include <agent/AgentToolManager.hpp>
#include <controllers/CommandHandler.hpp>
#include <fmt/core.h>
#include <model/QQMessage.hpp>
#include <service/OneBotClient.hpp>
#include <service/SessionConfigManager.hpp>
#include <spdlog/spdlog.h>
#include <sstream>
#include <storage/AdminStore.hpp>
#include <storage/SessionStore.hpp>
#include <util/CommonUtil.hpp>
#include <vector>

namespace insoulforge {
    namespace {
        /// @brief 跳过消息开头的空白与 [CQ:xxx] 段，返回首个有效字符的下标
        size_t skipToCommandText(std::string_view rawMsg) {
            size_t pos = 0;
            while (pos < rawMsg.length()) {
                while (pos < rawMsg.length() && std::isspace(static_cast<unsigned char>(rawMsg[pos]))) {
                    pos++;
                }
                if (pos < rawMsg.length() && rawMsg[pos] == '[') {
                    if (const size_t end = rawMsg.find(']', pos); end != std::string::npos) {
                        pos = end + 1;
                        continue;
                    }
                }
                break;
            }
            return pos;
        }
    } // namespace

    bool isCommand(const QQMessage &message) {
        // 群聊命令需要 @ 机器人；私聊消息本身就是对机器人说的，无需 @
        if (!message.atMe() && !message.isPrivate())
            return false;
        const std::string rawMsg = message.getRawMessage();
        const size_t pos = skipToCommandText(rawMsg);
        return pos < rawMsg.length() && rawMsg[pos] == '/';
    }

    drogon::Task<std::string> handleCommand(const QQMessage &message) {
        std::string rawMsg = message.getRawMessage();
        uint64_t sessionId = message.getSessionId(); ///< 会话 ID（群聊=群号；私聊=用户QQ号|私聊标志位）
        uint64_t senderQQ = message.getSenderQQNumber();

        bool hasPermission = AdminStore::isAdmin(senderQQ);

        std::string cmdStr;
        if (const size_t pos = skipToCommandText(rawMsg); pos < rawMsg.length() && rawMsg[pos] == '/') {
            cmdStr = rawMsg.substr(pos);
        }

        std::istringstream iss(cmdStr);
        std::string cmd;
        iss >> cmd;

        std::string response;

        if (cmd == "/help" || cmd == "/帮助") {
            response = "可用命令:\n"
                       "【会话管理】\n"
                       "/enable [会话ID] - 启用当前会话（群聊传群号，私聊可不带参数）\n"
                       "/disable [会话ID] - 禁用当前会话（私聊可不带参数）\n"
                       "/groups - 查看启用的会话列表\n"
                       "/status - 查看当前会话状态\n"
                       "【管理员】\n"
                       "/admins - 查看管理员列表\n"
                       "/addadmin <QQ号> - 添加管理员\n"
                       "/deladmin <QQ号> - 移除管理员\n"
                       "【表情管理】\n"
                       "/delemoji <名称> - 删除表情包\n"
                       "/listemoji - 查看表情包列表\n"
                       "【其他】\n"
                       "/help - 显示帮助\n"
                       "/about - 关于本项目\n\n"
                       "注意: 管理命令仅限管理员使用";
        } else if (cmd == "/status" || cmd == "/状态") {
            bool enabled = SessionStore::isSessionEnabled(sessionId);
            auto [allMesCount, allCharCount] = SessionConfigManager::getConfig(sessionId);
            response = fmt::format("会话 {} 状态:\n"
                                   "- 启用: {}\n"
                                   "- 消息数: {}\n"
                                   "- 字符数: {}",
              sessionId, enabled ? "是" : "否", allMesCount, allCharCount);
        } else if (cmd == "/admins" || cmd == "/管理员") {
            auto admins = AdminStore::getAdmins();
            response = "管理员列表:\n";
            for (auto qq: admins) {
                response += fmt::format("- {}\n", qq);
            }
            if (admins.empty()) {
                response = "暂无管理员";
            }
        } else if (cmd == "/about" || cmd == "/关于") {
            response = "InSoulForge\n"
                       "基于 Agent 架构，支持自定义角色、长期记忆、多工具调用\n\n"
                       "项目地址: https://github.com/DreamDonghao/insoulforge\n"
                       "作者: DreamDonghao\n"
                       "许可证: AGPL-3.0 (未经允许禁止商用)";
        } else if (!hasPermission) {
            response = fmt::format("权限不足，你({})不是管理员", senderQQ);
        } else if (cmd == "/enable" || cmd == "/启用") {
            uint64_t targetSession = sessionId;
            if (std::string arg; iss >> arg) {
                if (const auto parsed = tryParseUInt64(arg)) {
                    targetSession = *parsed;
                } else {
                    co_return "无效的ID格式";
                }
            }
            SessionStore::enableSession(targetSession);
            response = fmt::format("已启用会话: {}", targetSession);
        } else if (cmd == "/disable" || cmd == "/禁用") {
            uint64_t targetSession = sessionId;
            if (std::string arg; iss >> arg) {
                if (const auto parsed = tryParseUInt64(arg)) {
                    targetSession = *parsed;
                } else {
                    co_return "无效的ID格式";
                }
            }
            SessionStore::disableSession(targetSession);
            response = fmt::format("已禁用会话: {}", targetSession);
        } else if (cmd == "/groups" || cmd == "/群列表") {
            auto groups = SessionStore::getEnabledGroups();
            response = "启用的群聊列表:\n";
            for (auto gid: groups) {
                response += fmt::format("- {}\n", gid);
            }
            if (groups.empty()) {
                response = "没有启用的群聊";
            }
        } else if (cmd == "/addadmin" || cmd == "/添加管理员") {
            std::string arg;
            if (!(iss >> arg)) {
                co_return "用法: /addadmin <QQ号>";
            }
            if (const auto qq = tryParseUInt64(arg)) {
                AdminStore::addAdmin(*qq);
                response = fmt::format("已添加管理员: {}", *qq);
            } else {
                response = "无效的QQ号格式";
            }
        } else if (cmd == "/deladmin" || cmd == "/移除管理员") {
            std::string arg;
            if (!(iss >> arg)) {
                co_return "用法: /deladmin <QQ号>";
            }
            if (const auto qq = tryParseUInt64(arg)) {
                AdminStore::removeAdmin(*qq);
                response = fmt::format("已移除管理员: {}", *qq);
            } else {
                response = "无效的QQ号格式";
            }
        } else if (cmd == "/delemoji" || cmd == "/删除表情") {
            std::string name;
            if (!(iss >> name)) {
                co_return "用法: /delemoji <名称或序号>";
            }
            Json::Value emoji = co_await AgentToolManager::findFavoriteEmoji(name);
            if (emoji.isNull()) {
                co_return fmt::format("收藏表情中找不到'{}'", name);
            }

            if (!co_await OneBotClient::deleteCustomFace(emoji["res_id"].asString())) {
                co_return fmt::format("删除失败: {}（QQ 客户端操作失败）", name);
            }
            AgentToolManager::invalidateFavoriteEmojiCache();
            response = fmt::format("已从收藏表情中删除: {}", emoji["name"].asString());
        } else if (cmd == "/listemoji" || cmd == "/表情列表") {
            if (const Json::Value emojis = co_await AgentToolManager::fetchFavoriteEmojis(); emojis.empty()) {
                response = "QQ收藏表情为空或获取失败";
            } else {
                response = "收藏表情列表:\n";
                for (const auto &emoji: emojis) {
                    response += fmt::format("- {}\n", emoji["name"].asString());
                }
                response += fmt::format("\n共 {} 个表情", emojis.size());
            }
        } else {
            response = fmt::format("未知命令: {}\n使用 /help 查看可用命令", cmd);
        }

        co_return response;
    }
} // namespace insoulforge
