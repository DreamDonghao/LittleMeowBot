/// @file AgentToolManager.cpp
/// @brief Agent 工具管理器 - 实现
/// @author donghao
/// @date 2026-04-02

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

#include <agent/AgentToolManager.hpp>
#include <chrono>
#include <config/Config.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <model/QQMessage.hpp>
#include <mutex>
#include <random>
#include <set>
#include <tuple>
#include <util/CommonUtil.hpp>
#include <util/HttpUtil.hpp>
#include <util/JsonUtil.hpp>
#include <util/Logger.hpp>

#include <service/LongTermMemory.hpp>
#include <service/OneBotClient.hpp>
#include <service/TaskScheduler.hpp>
#include <service/ToolRegistry.hpp>
#include <storage/TaskStore.hpp>
#include <storage/ToolStore.hpp>

namespace insoulforge {
    namespace {
        /// @brief 构建 {"type":"object"} 形式的参数 schema（properties/required 初始化为空容器）
        json objectSchema() {
            json schema;
            schema["type"] = "object";
            schema["properties"] = json::object();
            schema["required"] = json::array();
            return schema;
        }

        void addStringParam(json &schema, const char *name, const std::string &description) {
            schema["properties"][name]["type"] = "string";
            schema["properties"][name]["description"] = description;
        }

        void addIntParam(json &schema, const char *name, const std::string &description) {
            schema["properties"][name]["type"] = "integer";
            schema["properties"][name]["description"] = description;
        }

        void addBoolParam(json &schema, const char *name, const std::string &description) {
            schema["properties"][name]["type"] = "boolean";
            schema["properties"][name]["description"] = description;
        }

        void requireParam(json &schema, const char *name) { schema["required"].push_back(name); }

        /// @brief 读取字符串参数（缺失按空串处理）
        std::string argString(const json &args, const char *key) { return getStr(args, key); }

        // ========== 终端工具 ==========

        void registerTerminalTools() {
            auto &registry = ToolRegistry::instance();

            // no_reply
            registry.registerTool(
              {.name = "no_reply",
                .description = "决定不回复消息。当：话题已参与过、没人问你、刚说过话、纯表情刷屏时使用。",
                .parameters = json(),
                .handler = [](const json &) -> drogon::Task<std::string> { co_return "ok"; }},
              ToolCategory::TERMINAL);

            // reply
            json replyParams = objectSchema();
            addStringParam(replyParams, "content", "要发送的回复内容");
            requireParam(replyParams, "content");
            registry.registerTool(
              {.name = "reply",
                .description = "回复消息。当：有人开启新的话题、有人问你、有人@你、有人求助时使用。",
                .parameters = replyParams,
                .handler = [](const json &) -> drogon::Task<std::string> { co_return "ok"; }},
              ToolCategory::TERMINAL);

            // reply_with_quote - 引用回复（TERMINAL，直接发送）
            json quoteReplyParams = objectSchema();
            addStringParam(quoteReplyParams, "content", "要发送的回复内容");
            addStringParam(quoteReplyParams, "message_id", "要引用的消息ID（从聊天记录JSON的message_id字段获取）");
            requireParam(quoteReplyParams, "content");
            requireParam(quoteReplyParams, "message_id");
            registry.registerTool(
              {.name = "reply_with_quote",
                .description = "引用回复特定消息。当需要回复特定消息、回答特定问题、澄清上下文时使用。聊"
                               "天记录格式为JSON：{\"message_id\":\"12345\",\"text\":\"...\"}，用 "
                               "message_id 字段的值作为参数。这是终端工具，调用后直接发送。",
                .parameters = quoteReplyParams,
                .handler = [](const json &) -> drogon::Task<std::string> { co_return "ok"; }},
              ToolCategory::TERMINAL);
        }

        // ========== 信息工具 ==========

        void registerInfoTools() {
            auto &registry = ToolRegistry::instance();

            // list_stickers
            registry.registerTool({.name = "list_stickers",
                                    .description = "获取QQ收藏表情中所有可用的表情名称列表。",
                                    .parameters = json(),
                                    .handler = [](const json &) -> drogon::Task<std::string> {
                                        const auto sessionId = currentToolContext().sessionId;
                                        const json emojis = co_await AgentToolManager::fetchFavoriteEmojis(sessionId);
                                        if (emojis.empty()) {
                                            co_return std::string("表情库为空（QQ收藏表情列表获取失败或没有收藏表情）");
                                        }
                                        std::string result = "可用表情: ";
                                        bool first = true;
                                        for (const auto &emoji: emojis) {
                                            if (!first)
                                                result += ", ";
                                            result += getStr(emoji, "name");
                                            first = false;
                                        }
                                        co_return result;
                                    }},
              ToolCategory::INFORMATION);

            // recall_memory
            json memoryParams = objectSchema();
            addStringParam(memoryParams, "query", "要回忆的内容关键词，如某人的喜好、某群的习惯等");
            requireParam(memoryParams, "query");
            registry.registerTool(
              {.name = "recall_memory",
                .description = "从长期记忆库中回忆信息（如果需要可以先获取当前群聊的名称）。当想不起某人"
                               "喜好、某群习惯、过去的约定时使用。模拟人类回忆过程。",
                .parameters = memoryParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    const std::string query = argString(args, "query");
                    if (query.empty())
                        co_return std::string("请提供回忆关键词");

                    const auto [sessionId, groupName] = currentToolContext();
                    const auto result = co_await LongTermMemory::searchMemory(query, 3, sessionId);
                    if (!result || result->empty()) {
                        co_return "想不起来了，没有找到相关记忆";
                    }
                    co_return "回忆起：" + result.value();
                }},
              ToolCategory::INFORMATION);

            // get_group_name
            registry.registerTool({.name = "get_group_name",
                                    .description = "获取当前群聊的名称。当需要知道群名或确认当前群时使用。",
                                    .parameters = json(),
                                    .handler = [](const json &) -> drogon::Task<std::string> {
                                        const auto &[sessionId, groupName] = currentToolContext();
                                        if (QQMessage::isPrivateSession(sessionId)) {
                                            co_return std::string("当前是私聊，没有群名");
                                        }
                                        if (sessionId == 0) {
                                            co_return "无法获取群信息";
                                        }
                                        if (!groupName.empty()) {
                                            co_return fmt::format("当前群：{}（群号：{}）", groupName, sessionId);
                                        }
                                        co_return fmt::format("当前群号：{}", sessionId);
                                    }},
              ToolCategory::INFORMATION);
        }

        // ========== CQ 码工具（获取需拼入 reply content 的 CQ 码） ==========

        void registerCqTools() {
            auto &registry = ToolRegistry::instance();

            // send_face
            json faceParams = objectSchema();
            addIntParam(faceParams, "id",
              "表情ID，常用: 1-发呆, 2-撇嘴, 3-色, 4-发呆, 5-得意, 6-流泪, 7-害羞, 8-闭嘴, 9-睡, 10-大哭, 11-尴尬, "
              "12-发怒, 13-调皮, 14-呲牙, 15-惊讶, 16-难过, 17-酷, 18-冷汗, 19-抓狂, 20-吐, 21-偷笑, 22-可爱, "
              "23-白眼, 24-傲慢, 25-饥饿, 26-困, 27-惊恐, 28-流汗, 29-憨笑, 30-大兵, 31-奋斗, 32-咒骂, 33-疑问, "
              "34-嘘, 35-晕, 36-折磨, 37-衰, 38-骷髅, 39-敲打, 40-再见");
            requireParam(faceParams, "id");
            registry.registerTool({.name = "send_face",
                                    .description = "获取QQ原生表情的CQ码。返回的CQ码必须复制到reply的content中。",
                                    .parameters = faceParams,
                                    .handler = [](const json &args) -> drogon::Task<std::string> {
                                        const int id = getInt(args, "id", 1);
                                        co_return fmt::format("[CQ:face,id={}]", id);
                                    }},
              ToolCategory::ACTION);

            // send_image
            json imageParams = objectSchema();
            addStringParam(imageParams, "url", "图片URL地址");
            requireParam(imageParams, "url");
            registry.registerTool(
              {.name = "send_image",
                .description = "获取网络图片的CQ码。提供图片URL。返回的CQ码必须复制到reply的content中。",
                .parameters = imageParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    std::string url = argString(args, "url");
                    if (url.empty())
                        co_return std::string("请提供图片URL");
                    co_return fmt::format("[CQ:image,file={}]", url);
                }},
              ToolCategory::ACTION);
        }

        // ========== 收藏表情工具 ==========

        void registerStickerTools() {
            auto &registry = ToolRegistry::instance();

            // send_sticker
            json stickerParams = objectSchema();
            addStringParam(stickerParams, "name", "表情名称（先调list_stickers查看可用名称）");
            requireParam(stickerParams, "name");
            registry.registerTool(
              {.name = "send_sticker",
                .description =
                  "获取QQ收藏表情的CQ码。先调list_"
                  "stickers查看可用表情名，再用此工具。返回的CQ码必须复制到reply的content中，否则表情不会显示。",
                .parameters = stickerParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    std::string name = argString(args, "name");
                    if (name.empty())
                        co_return std::string("请提供表情名称");

                    const auto sessionId = currentToolContext().sessionId;
                    json emoji = co_await AgentToolManager::findFavoriteEmoji(name, sessionId);
                    if (emoji.is_null()) {
                        co_return fmt::format("表情'{}'不存在，先调list_stickers查看可用表情", name);
                    }

                    // 商城表情（字段齐全）走 mface；个人收藏表情走 image+sub_type=1（QQ 渲染为表情）
                    if (getBool(emoji, "is_mark_face") && !getStr(emoji, "emoji_id").empty() &&
                        !getStr(emoji, "key").empty()) {
                        co_return fmt::format("[CQ:mface,summary={},emoji_id={},emoji_package_id={},key={}]",
                          getStr(emoji, "summary"), getStr(emoji, "emoji_id"), getStr(emoji, "emoji_package_id"),
                          getStr(emoji, "key"));
                    }

                    if (getStr(emoji, "url").empty()) {
                        co_return fmt::format("表情'{}'缺少图片地址，无法发送", name);
                    }
                    co_return fmt::format(
                      "[CQ:image,file={},sub_type=1,summary={}]", getStr(emoji, "url"), getStr(emoji, "summary"));
                }},
              ToolCategory::ACTION);

            // save_sticker - 保存别人发的表情为QQ收藏表情
            json saveParams = objectSchema();
            addStringParam(saveParams, "file", "图片在QQ缓存中的文件名（来自聊天记录JSON的images[].file字段）");
            addStringParam(saveParams, "url",
              "图片URL（来自聊天记录JSON的images[].url字段），file方式获取失败时用于下载，最好同时提供");
            addStringParam(saveParams, "name", "给表情起的简短名字（根据图片内容），如: 摸头、猫猫惊讶");
            requireParam(saveParams, "file");
            requireParam(saveParams, "name");
            registry.registerTool(
              {.name = "save_sticker",
                .description = "把用户发的表情/"
                               "图片保存为自己的QQ收藏表情并设置描述名称。仅在用户明确要求保存表情时使用。聊天记录中图"
                               "片消息会带images数组，同时传images[].file和images[]."
                               "url作为参数。name必须起一个能体现图片内容的名字，方便以后用send_sticker引用。",
                .parameters = saveParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    const auto sessionId = currentToolContext().sessionId;
                    const std::string file = argString(args, "file");
                    const std::string url = argString(args, "url");
                    const std::string name = argString(args, "name");
                    if (file.empty())
                        co_return std::string("请提供图片文件名(file)");
                    if (name.empty())
                        co_return std::string("请提供表情名称(name)");

                    spdlog::info("[Sticker] save_sticker 参数: file={} url={}", file, url.substr(0, 150));

                    // Step 1: 尝试 get_image 拿容器内路径（商城表情会失败/超时）
                    std::string containerPath;
                    if (const auto path = co_await OneBotClient::getImage(file, sessionId)) {
                        containerPath = *path;
                    }

                    // Step 2: get_image 失败则回退到 download_file（URL 下载进容器）
                    if (containerPath.empty()) {
                        if (url.empty()) {
                            co_return std::string("获取图片失败，请确认图片仍可访问");
                        }
                        if (const auto path = co_await OneBotClient::downloadFile(url, sessionId)) {
                            containerPath = *path;
                        }
                        if (containerPath.empty()) {
                            co_return std::string("获取图片失败，可能是图片链接已过期，请让对方重新发送后立即保存");
                        }
                    }

                    // Step 3: 记录保存前的 res_id 集合，用于保存后定位新表情
                    AgentToolManager::invalidateFavoriteEmojiCache();
                    std::set<std::string> beforeIds;
                    for (const auto &e: co_await AgentToolManager::fetchFavoriteEmojis(sessionId)) {
                        if (const std::string rid = getStr(e, "res_id"); !rid.empty()) {
                            beforeIds.insert(rid);
                        }
                    }

                    // Step 4: add_custom_face 保存为收藏表情
                    if (!co_await OneBotClient::addCustomFace(containerPath, sessionId)) {
                        co_return std::string("保存为收藏表情失败");
                    }

                    // Step 5: 定位新表情并设置描述
                    AgentToolManager::invalidateFavoriteEmojiCache();
                    json newItem;
                    for (const auto &e: co_await AgentToolManager::fetchFavoriteEmojis(sessionId)) {
                        if (const std::string rid = getStr(e, "res_id"); !rid.empty() && !beforeIds.contains(rid)) {
                            newItem = e;
                            break;
                        }
                    }
                    if (!newItem.is_null()) {
                        const std::string emojiId =
                          getStr(newItem, "emoji_id").empty() ? "0" : getStr(newItem, "emoji_id");
                        if (co_await OneBotClient::setCustomFaceDesc(
                              emojiId, getStr(newItem, "res_id"), getStr(newItem, "md5"), name, sessionId)) {
                            AgentToolManager::invalidateFavoriteEmojiCache();
                        } else {
                            spdlog::warn("[Sticker] 设置表情描述失败: {}", getStr(newItem, "res_id"));
                        }
                    }

                    spdlog::info("[Sticker] 已保存收藏表情: {} ({})", containerPath, name);
                    co_return fmt::format("已保存为收藏表情，名称: {}", name);
                }},
              ToolCategory::ACTION);

            // rename_sticker - 修改收藏表情的名称/描述
            json renameParams = objectSchema();
            addStringParam(renameParams, "name", "要改名的表情当前名称（先用list_stickers查看）");
            addStringParam(renameParams, "new_name", "新名称，简短体现图片内容");
            requireParam(renameParams, "name");
            requireParam(renameParams, "new_name");
            registry.registerTool({.name = "rename_sticker",
                                    .description = "修改收藏表情的名称/"
                                                   "描述。仅在用户明确要求给表情改名时使用。先调list_"
                                                   "stickers查看当前名称，再把新名称传给new_name。",
                                    .parameters = renameParams,
                                    .handler = [](const json &args) -> drogon::Task<std::string> {
                                        const std::string name = argString(args, "name");
                                        const std::string newName = argString(args, "new_name");
                                        if (name.empty())
                                            co_return std::string("请提供表情当前名称(name)");
                                        if (newName.empty())
                                            co_return std::string("请提供新名称(new_name)");

                                        const auto sessionId = currentToolContext().sessionId;
                                        json emoji = co_await AgentToolManager::findFavoriteEmoji(name, sessionId);
                                        if (emoji.is_null()) {
                                            co_return fmt::format(
                                              "表情'{}'不存在，先调list_stickers查看可用表情", name);
                                        }

                                        if (!co_await OneBotClient::setCustomFaceDesc(
                                              getStr(emoji, "emoji_id").empty() ? "0" : getStr(emoji, "emoji_id"),
                                              getStr(emoji, "res_id"), getStr(emoji, "md5"), newName, sessionId)) {
                                            co_return std::string("改名失败");
                                        }

                                        AgentToolManager::invalidateFavoriteEmojiCache();
                                        spdlog::info("[Sticker] 表情改名: {} -> {}", name, newName);
                                        co_return fmt::format("已改名为: {}", newName);
                                    }},
              ToolCategory::ACTION);

            // delete_sticker - 从收藏表情中删除
            json delParams = objectSchema();
            addStringParam(delParams, "name", "要删除的表情名称（先用list_stickers查看）");
            requireParam(delParams, "name");
            registry.registerTool(
              {.name = "delete_sticker",
                .description = "从QQ收藏表情中删除表情。仅在用户明确要求删除表情时使用，删除前先确认名称无误。先调list"
                               "_stickers查看名称。",
                .parameters = delParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    std::string name = argString(args, "name");
                    if (name.empty())
                        co_return std::string("请提供表情名称(name)");

                    const auto sessionId = currentToolContext().sessionId;
                    json emoji = co_await AgentToolManager::findFavoriteEmoji(name, sessionId);
                    if (emoji.is_null()) {
                        co_return fmt::format("表情'{}'不存在，先调list_stickers查看可用表情", name);
                    }

                    if (!co_await OneBotClient::deleteCustomFace(getStr(emoji, "res_id"), sessionId)) {
                        co_return std::string("删除失败");
                    }

                    AgentToolManager::invalidateFavoriteEmojiCache();
                    spdlog::info("[Sticker] 已删除收藏表情: {}", name);
                    co_return fmt::format("已删除表情: {}", name);
                }},
              ToolCategory::ACTION);
        }

        // ========== 群互动动作工具 ==========

        void registerGroupActionTools() {
            auto &registry = ToolRegistry::instance();

            // at_user
            json atParams = objectSchema();
            addStringParam(atParams, "qq", "要@的QQ号（从聊天记录JSON的sender.qq字段获取）。使用 'all' @全体成员");
            requireParam(atParams, "qq");
            registry.registerTool(
              {.name = "at_user",
                .description =
                  "@某人。返回CQ码嵌入reply的content中。聊天记录格式为JSON：{\"sender\":{\"name\":\"小明\","
                  "\"qq\":\"123456\"}}，用 at_user(qq=\"123456\") 来@他。@全体成员用 at_user(qq=\"all\")",
                .parameters = atParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    if (QQMessage::isPrivateSession(currentToolContext().sessionId)) {
                        co_return std::string("私聊中无法@成员，直接回复即可");
                    }
                    std::string qq = argString(args, "qq");
                    if (qq.empty())
                        co_return std::string("请提供QQ号");
                    if (qq == "all") {
                        co_return std::string("[CQ:at,qq=all]");
                    }
                    co_return fmt::format("[CQ:at,qq={}]", qq);
                }},
              ToolCategory::ACTION);

            // ban_user
            json banParams = objectSchema();
            addStringParam(banParams, "qq", "要禁言的QQ号（从聊天记录JSON的sender.qq字段获取）");
            addIntParam(
              banParams, "duration", "禁言时长（秒）。轻度60-300秒，中度600-1800秒，重度3600秒以上。0解除禁言");
            requireParam(banParams, "qq");
            registry.registerTool(
              {.name = "ban_user",
                .description = "禁言群成员。要有自己的判断，不要别人让你禁言就禁言。根据违规程度选择时长：轻度("
                               "偶尔骂人)60-300秒，中度(持续刷屏骂人)600-1800秒，重度(恶意骚扰)3600秒+",
                .parameters = banParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    const uint64_t sessionId = currentToolContext().sessionId;
                    if (QQMessage::isPrivateSession(sessionId))
                        co_return std::string("私聊中无法禁言");
                    if (sessionId == 0)
                        co_return std::string("禁言失败: 无法获取群号");

                    const uint64_t userId = parseUInt64(argString(args, "qq"));
                    const uint64_t duration = getUInt(args, "duration", 600);
                    if (userId == 0)
                        co_return std::string("禁言失败: 请提供有效的QQ号");

                    const bool success = co_await OneBotClient::setGroupBan(sessionId, userId, duration);
                    co_return success ? fmt::format("已禁言用户 {} {}秒", userId, duration)
                                      : "禁言失败: 权限不足或用户不存在";
                }},
              ToolCategory::ACTION);

            // send_poke
            json pokeParams = objectSchema();
            addStringParam(pokeParams, "qq", "要拍一拍的QQ号（从聊天记录JSON的sender.qq字段获取）");
            requireParam(pokeParams, "qq");
            registry.registerTool(
              {.name = "send_poke",
                .description =
                  "拍一拍群成员。用于打招呼、引起注意、开玩笑等轻松互动场景。聊天记录格式为JSON：{\"sender\":{"
                  "\"name\":\"小明\",\"qq\":\"123456\"}}，用 send_poke(qq=\"123456\") 来拍他。",
                .parameters = pokeParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    const uint64_t sessionId = currentToolContext().sessionId;
                    if (QQMessage::isPrivateSession(sessionId)) {
                        co_return std::string("私聊中不支持拍一拍，直接回复即可");
                    }
                    if (sessionId == 0)
                        co_return std::string("拍一拍失败: 无法获取群号");

                    const uint64_t userId = parseUInt64(argString(args, "qq"));
                    if (userId == 0)
                        co_return std::string("拍一拍失败: 请提供有效的QQ号");

                    const bool success = co_await OneBotClient::sendPoke(sessionId, userId);
                    co_return success ? fmt::format("已拍一拍用户 {}", userId) : "拍一拍失败: 权限不足或用户不存在";
                }},
              ToolCategory::ACTION);

            // recall_message
            json recallParams = objectSchema();
            addStringParam(
              recallParams, "message_id", "要撤回的消息ID（从聊天记录JSON的message_id或reply_to字段获取）");
            requireParam(recallParams, "message_id");
            registry.registerTool(
              {.name = "recall_message",
                .description = "撤回消息。当用户要求撤回某条消息时使用。聊天记录格式为JSON：{\"message_"
                               "id\":\"12345\",\"reply_to\":\"67890\"}。若用户想撤回引用的消息，用 "
                               "reply_to 字段的值；若想撤回某条消息本身，用 message_id 字段的值。",
                .parameters = recallParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    const uint64_t messageId = parseUInt64(argString(args, "message_id"));
                    if (messageId == 0)
                        co_return std::string("撤回失败: 请提供有效的消息ID");

                    const auto sessionId = currentToolContext().sessionId;
                    const bool success = co_await OneBotClient::deleteMsg(messageId, sessionId);
                    co_return success ? fmt::format("已撤回消息 {}", messageId)
                                      : "撤回失败: 消息可能已超过2分钟或权限不足";
                }},
              ToolCategory::ACTION);
        }

        // ========== 定时任务工具 ==========

        void registerSchedulerTools() {
            auto &registry = ToolRegistry::instance();

            // create_scheduled_task - 创建定时提醒任务
            json scheduleParams = objectSchema();
            addStringParam(scheduleParams, "time",
              "触发时间，必须是 YYYY-MM-DD HH:MM:SS 格式的绝对时间。根据聊天记录中最新消息的 time 字段推算当前时间，"
              "把用户说的『明天6点』『一小时后』换算成完整日期时间再传入");
            addStringParam(scheduleParams, "content",
              fmt::format("到点时留给自己（{}）的备忘说明，不是最终的回复文本：写清楚要提醒谁（带上对方昵称及sender."
                          "qq）、要做什么事、"
                          "以及创建时对话里的相关背景。到点后你会看到这段备忘并结合当时的聊天上下文自行组织回复",
                Config::instance().botName));
            addBoolParam(scheduleParams, "daily",
              "可选，默认 false。当用户要求每天固定时间重复提醒时传 true（如'每天早上8点叫我起床'），"
              "此时 time 填下一次触发的完整日期时间，之后每天同一时刻自动触发");
            requireParam(scheduleParams, "time");
            requireParam(scheduleParams, "content");
            registry.registerTool(
              {.name = "create_scheduled_task",
                .description = "创建定时提醒任务。当用户明确要求在未来某个时刻提醒/"
                               "通知某事时使用（如'明天6点叫我起床''两小时后提醒我开会'）。"
                               "到点后会以【系统定时任务】消息回到当前会话，你再据此生成提醒回复。",
                .parameters = scheduleParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    const std::string content = argString(args, "content");
                    if (content.empty())
                        co_return std::string("请提供提醒内容(content)");
                    if (content.size() > 500)
                        co_return std::string("提醒内容过长（最多500字符），请精简");

                    const auto remindTime = TaskScheduler::parseTimeString(argString(args, "time"));
                    if (!remindTime) {
                        co_return std::string("无法识别时间格式，请按 YYYY-MM-DD HH:MM:SS 提供换算后的完整绝对时间");
                    }

                    const time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    if (*remindTime <= now + 10) {
                        co_return fmt::format("时间无效：必须晚于当前时间10秒以上。当前时间是 {}", currentDateTime());
                    }
                    if (*remindTime > now + 366LL * 24 * 3600) {
                        co_return std::string("时间过早远：不允许设置超过一年后的提醒");
                    }

                    const uint64_t sessionId = currentToolContext().sessionId;
                    if (sessionId == 0)
                        co_return std::string("会话上下文缺失，无法确定提醒目标");
                    const bool isPrivateSession = QQMessage::isPrivateSession(sessionId);
                    const auto [sessionType, targetId] = QQMessage::parseSessionTarget(sessionId);
                    const bool isDaily = getBool(args, "daily");

                    TaskStore::ScheduledTask task;
                    task.sessionType = sessionType;
                    task.targetId = targetId;
                    task.remindTime = *remindTime;
                    task.content = content;
                    task.isDaily = isDaily;

                    try {
                        const int64_t id = TaskScheduler::instance().schedule(std::move(task));
                        if (isDaily) {
                            co_return fmt::format("每日定时任务 #{} 已创建，每天 {} 在{}触发提醒", id,
                              formatTimeOfDay(*remindTime), isPrivateSession ? "私聊" : "本群");
                        }
                        co_return fmt::format("定时任务 #{} 已创建，将于 {} 在{}触发提醒", id,
                          formatUnixTime(*remindTime), isPrivateSession ? "私聊" : "本群");
                    } catch (const std::exception &e) {
                        spdlog::error("[Scheduler] 创建定时任务入库失败: {}", e.what());
                        co_return std::string("创建定时任务失败，请稍后重试");
                    }
                }},
              ToolCategory::ACTION);

            // list_scheduled_tasks - 查看当前会话的定时任务
            registry.registerTool(
              {.name = "list_scheduled_tasks",
                .description = "查看当前会话所有待触发的定时任务。当用户想确认已设置的提醒、或取消前需要获取任务编号"
                               "时使用。返回任务编号、触发时间和备忘内容。",
                .parameters = json(),
                .handler = [](const json &) -> drogon::Task<std::string> {
                    const uint64_t sessionId = currentToolContext().sessionId;
                    if (sessionId == 0)
                        co_return std::string("会话上下文缺失，无法查询定时任务");
                    const auto [sessionType, targetId] = QQMessage::parseSessionTarget(sessionId);
                    const auto tasks = TaskStore::getPendingScheduledTasksByTarget(sessionType, targetId);
                    if (tasks.empty()) {
                        co_return std::string("当前会话没有待触发的定时任务");
                    }

                    std::string out = fmt::format("当前会话共有 {} 个待触发定时任务：\n", tasks.size());
                    for (const auto &task: tasks) {
                        if (task.isDaily) {
                            out += fmt::format("- 任务#{}：每天 {} 「{}」（每日重复）\n", task.id,
                              formatTimeOfDay(task.remindTime), task.content);
                        } else {
                            out += fmt::format(
                              "- 任务#{}：{} 「{}」\n", task.id, formatUnixTime(task.remindTime), task.content);
                        }
                    }
                    out += "如需取消某个任务，调用 cancel_scheduled_task 并传入任务编号（#后的数字）";
                    co_return out;
                }},
              ToolCategory::INFORMATION);

            // cancel_scheduled_task - 取消定时任务
            json cancelTaskParams = objectSchema();
            addStringParam(
              cancelTaskParams, "task_id", "要取消的任务编号（用 list_scheduled_tasks 查询，即任务#后的数字）");
            requireParam(cancelTaskParams, "task_id");
            registry.registerTool(
              {.name = "cancel_scheduled_task",
                .description = "取消尚未触发的定时任务（含每日重复任务）。当用户要求取消之前的提醒/定时任务时使用；"
                               "如不知道任务编号，先用 list_scheduled_tasks 查询。",
                .parameters = cancelTaskParams,
                .handler = [](const json &args) -> drogon::Task<std::string> {
                    const int64_t taskId = static_cast<int64_t>(parseUInt64(argString(args, "task_id")));
                    if (taskId == 0)
                        co_return std::string("请提供有效的任务编号（可先用 list_scheduled_tasks 查询）");

                    co_return TaskScheduler::instance().cancel(taskId)
                      ? fmt::format("已取消定时任务 #{}", taskId)
                      : fmt::format("取消失败：任务 #{} 不存在或已触发/已取消", taskId);
                }},
              ToolCategory::ACTION);
        }
    } // namespace

    void AgentToolManager::registerAllTools() {
        registerTerminalTools();
        registerInfoTools();
        registerCqTools();
        registerStickerTools();
        registerGroupActionTools();
        registerSchedulerTools();

        spdlog::info("ToolManager: 工具注册完成（共20个内置工具）");
    }

    void AgentToolManager::registerCustomTools() {
        auto &registry = ToolRegistry::instance();

        // 先清除所有已注册的自定义工具
        registry.clearAllCustomTools();


        // 只注册启用的工具
        const auto tools = ToolStore::getEnabledCustomTools();
        int count = 0;

        for (const auto &tool: tools) {
            json params;
            if (!tool.parameters.empty()) {
                std::ignore = tryParseJson(tool.parameters, params);
                // 确保顶层 type 为 object（OpenAI function calling 要求）
                if (!params.is_null() && !params.contains("type")) {
                    params["type"] = "object";
                }
            }

            // 根据执行类型注册不同的 handler
            if (tool.executorType == "python") {
                registry.registerTool(
                  {.name = tool.name,
                    .description = tool.description,
                    .parameters = params,
                    .handler = [script = tool.scriptContent](const json &args) -> drogon::Task<std::string> {
                        co_return co_await executePythonTool(script, args);
                    }},
                  ToolCategory::INFORMATION);
            } else if (tool.executorType == "http") {
                registry.registerTool(
                  {.name = tool.name,
                    .description = tool.description,
                    .parameters = params,
                    .handler = [config = tool.executorConfig](const json &args) -> drogon::Task<std::string> {
                        co_return co_await executeHttpTool(config, args);
                    }},
                  ToolCategory::INFORMATION);
            }

            registry.recordCustomTool(tool.name);
            count++;
            spdlog::info("ToolManager: 注册自定义工具 '{}' ({})", tool.name, tool.executorType);
        }

        spdlog::info("ToolManager: 自定义工具注册完成（共{}个）", count);
    }

    drogon::Task<std::string> AgentToolManager::executePythonTool(const std::string &scriptContent, const json &args) {
        if (scriptContent.empty()) {
            co_return std::string("脚本内容为空");
        }

        // 获取配置的Python解释器路径
        std::string pythonPath = ToolStore::getCustomToolPython();

        // 构建输入参数 JSON
        const std::string inputJson = dumpJson(args, false);

        // 创建临时脚本文件（析构时自动删除）
        struct TempFile {
            std::string path;

            explicit TempFile(std::string p) : path(std::move(p)) {}

            TempFile(const TempFile &) = delete;

            TempFile &operator=(const TempFile &) = delete;

            ~TempFile() {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        };

        std::random_device rd;
        const auto tmpDir = std::filesystem::temp_directory_path();
        TempFile tmpScript((tmpDir / ("tool_" + std::to_string(rd()) + ".py")).string());
        TempFile tmpInput((tmpDir / ("tool_input_" + std::to_string(rd()) + ".json")).string());

        // 写入脚本 - 去除开头可能的多余空白，保留内部缩进
        std::string cleanScript = scriptContent;
        // 去除开头的空白行
        if (size_t firstNonSpace = cleanScript.find_first_not_of(" \t\n\r");
          firstNonSpace != std::string::npos && firstNonSpace > 0) {
            cleanScript = cleanScript.substr(firstNonSpace);
        }

        {
            std::ofstream scriptFile(tmpScript.path);
            scriptFile << cleanScript;
        }
        {
            std::ofstream inputFile(tmpInput.path);
            inputFile << inputJson;
        }

        // 调试：打印脚本内容
        spdlog::debug("Python脚本内容:\n{}", cleanScript);

        // 执行: pythonPath script.py input.json
        std::string cmd = pythonPath + " " + tmpScript.path + " " + tmpInput.path + " 2>&1";
        spdlog::debug("执行Python工具: {}", cmd);

        std::array<char, 4096> buffer{};
        std::string result;

        // RAII 管理管道：出错路径自动 pclose
        struct PipeCloser {
            void operator()(FILE *f) const noexcept {
                if (f)
                    std::ignore = pclose(f);
            }
        };
        std::unique_ptr<FILE, PipeCloser> pipe(popen(cmd.c_str(), "r"));
        if (!pipe) {
            co_return std::string("执行脚本失败");
        }

        while (fgets(buffer.data(), buffer.size(), pipe.get())) {
            result += buffer.data();
        }

        if (const int exitCode = pclose(pipe.release()); exitCode != 0) {
            spdlog::warn("Python工具执行返回非零: {}, 输出: {}", exitCode, result);
        }

        // 移除末尾换行
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r')) {
            result.pop_back();
        }

        co_return result;
    }

    drogon::Task<std::string> AgentToolManager::executeHttpTool(const std::string &config, const json &args) {
        // 解析配置
        json configJson;
        if (!tryParseJson(config, configJson)) {
            spdlog::error("HTTP工具配置解析失败");
            co_return std::string("工具配置错误");
        }

        std::string url = getStr(configJson, "url");
        std::string method = getStr(configJson, "method", "POST");

        if (url.empty()) {
            co_return std::string("未配置URL");
        }

        // 解析 URL
        // 格式: http://host:port/path 或 https://host:port/path
        size_t protoEnd = url.find("://");
        if (protoEnd == std::string::npos) {
            co_return std::string("URL格式错误");
        }
        std::string proto = url.substr(0, protoEnd);
        std::string rest = url.substr(protoEnd + 3);

        size_t pathStart = rest.find('/');
        std::string hostPort = pathStart == std::string::npos ? rest : rest.substr(0, pathStart);
        std::string path = pathStart == std::string::npos ? "/" : rest.substr(pathStart);

        // 创建 HTTP 客户端并发送（GET 无 body，其余把 args 作为 JSON body）
        std::string baseUrl = proto + "://" + hostPort;
        const bool isGet = (method == "GET");
        const auto resp = co_await HttpUtil::send("[HttpTool]", baseUrl, path, isGet ? drogon::Get : drogon::Post,
          isGet ? json(nullptr) : args, "", 30.0, currentToolContext().sessionId);
        if (!resp) {
            co_return std::string("HTTP请求失败");
        }
        co_return std::string((*resp)->getBody());
    }

    namespace {
        // 收藏表情缓存（60秒TTL）
        std::mutex g_favEmojiCacheMutex;
        json g_favEmojiCache(nullptr);
        std::chrono::steady_clock::time_point g_favEmojiCacheTime{};

        // CQ码参数值不能包含逗号和方括号，替换为空格
        std::string sanitizeCqParam(std::string s) {
            for (char &c: s) {
                if (c == ',' || c == '[' || c == ']')
                    c = ' ';
            }
            return s;
        }
    } // namespace

    drogon::Task<json> AgentToolManager::fetchFavoriteEmojis(const std::optional<uint64_t> sessionId) {
        using namespace std::chrono_literals;
        {
            std::lock_guard lock(g_favEmojiCacheMutex);
            if (!g_favEmojiCache.is_null() && std::chrono::steady_clock::now() - g_favEmojiCacheTime < 60s) {
                co_return g_favEmojiCache;
            }
        }

        json result(json::array());
        const json data = co_await OneBotClient::fetchCustomFaceDetail(sessionId);

        int idx = 0;
        for (const auto &item: data) {
            json emoji;
            const std::string desc = sanitizeCqParam(getStr(item, "desc"));
            const std::string md5 = getStr(item, "md5");
            // 无名表情用 md5 前6位兜底（稳定且可区分），序号会随列表顺序漂移
            std::string fallback = md5.size() >= 6 ? "表情" + md5.substr(0, 6) : fmt::format("表情{}", idx + 1);
            emoji["name"] = desc.empty() ? fallback : desc;
            emoji["summary"] = desc;
            emoji["emoji_id"] = getStr(item, "eId");
            emoji["emoji_package_id"] = getStr(item, "epId");
            emoji["key"] = getStr(item, "key");
            emoji["url"] = getStr(item, "url");
            emoji["md5"] = md5;
            emoji["res_id"] = getStr(item, "resId");
            emoji["is_mark_face"] = getBool(item, "isMarkFace");
            result.push_back(emoji);
            idx++;
        }

        // 网络异常（resp 为空）时缓存空结果，避免高频重试
        {
            std::lock_guard lock(g_favEmojiCacheMutex);
            g_favEmojiCache = result;
            g_favEmojiCacheTime = std::chrono::steady_clock::now();
        }
        co_return result;
    }

    drogon::Task<json> AgentToolManager::findFavoriteEmoji(
      const std::string &name, const std::optional<uint64_t> sessionId) {
        for (const json emojis = co_await fetchFavoriteEmojis(sessionId); const auto &emoji: emojis) {
            if (getStr(emoji, "name") == name || getStr(emoji, "summary") == name) {
                co_return emoji;
            }
        }
        co_return json(nullptr);
    }

    void AgentToolManager::invalidateFavoriteEmojiCache() {
        std::lock_guard lock(g_favEmojiCacheMutex);
        g_favEmojiCache = json(nullptr);
    }
} // namespace insoulforge
