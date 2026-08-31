/// @file AgentToolManager.cpp
/// @brief Agent 工具管理器 - 实现
/// @author donghao
/// @date 2026-04-02

#ifdef _WIN32
#define popen  _popen
#define pclose _pclose
#endif

#include <agent/AgentToolManager.hpp>
#include <model/QQMessage.hpp>
#include <config/Config.hpp>
#include <util/HttpUtil.hpp>
#include <util/Logger.hpp>
#include <util/tool.h>
#include <fstream>
#include <memory>
#include <random>
#include <filesystem>
#include <chrono>
#include <mutex>
#include <set>
#include <tuple>

#include <service/OneBotClient.hpp>
#include "service/RAGFlowClient.hpp"
#include "service/ToolRegistry.hpp"
#include "service/TaskScheduler.hpp"
#include <storage/ToolStore.hpp>
#include <storage/TaskStore.hpp>

namespace insoulforge {
    void AgentToolManager::registerAllTools() {
        auto &registry = ToolRegistry::instance();

        // ========== 终端工具 ==========

        // no_reply
        registry.registerTool(
            {
                .name = "no_reply",
                .description = "决定不回复消息。当：话题已参与过、没人问你、刚说过话、纯表情刷屏时使用。",
                .parameters = Json::Value(),
                .handler = [](const Json::Value &) -> drogon::Task<std::string> { co_return "ok"; }
            }, ToolCategory::TERMINAL
        );

        // reply
        Json::Value replyParams;
        replyParams["type"] = "object";
        replyParams["properties"]["content"]["type"] = "string";
        replyParams["properties"]["content"]["description"] = "要发送的回复内容";
        replyParams["required"].append("content");
        registry.registerTool(
            {
                .name = "reply",
                .description = "回复消息。当：有人开启新的话题、有人问你、有人@你、有人求助时使用。",
                .parameters = replyParams,
                .handler = [](const Json::Value &) -> drogon::Task<std::string> { co_return "ok"; }
            }, ToolCategory::TERMINAL
        );

        // reply_with_quote - 引用回复（TERMINAL，直接发送）
        Json::Value quoteReplyParams;
        quoteReplyParams["type"] = "object";
        quoteReplyParams["properties"]["content"]["type"] = "string";
        quoteReplyParams["properties"]["content"]["description"] = "要发送的回复内容";
        quoteReplyParams["properties"]["message_id"]["type"] = "string";
        quoteReplyParams["properties"]["message_id"]["description"] = "要引用的消息ID（从聊天记录JSON的message_id字段获取）";
        quoteReplyParams["required"].append("content");
        quoteReplyParams["required"].append("message_id");
        registry.registerTool(
            {
                .name = "reply_with_quote",
                .description =
                "引用回复特定消息。当需要回复特定消息、回答特定问题、澄清上下文时使用。聊天记录格式为JSON：{\"message_id\":\"12345\",\"text\":\"...\"}，用 message_id 字段的值作为参数。这是终端工具，调用后直接发送。",
                .parameters = quoteReplyParams,
                .handler = [](const Json::Value &) -> drogon::Task<std::string> { co_return "ok"; }
            }, ToolCategory::TERMINAL
        );

        // ========== 信息工具 ==========
        // list_stickers
        registry.registerTool(
            {
                .name = "list_stickers",
                .description = "获取QQ收藏表情中所有可用的表情名称列表。",
                .parameters = Json::Value(),
                .handler = [](const Json::Value &) -> drogon::Task<std::string> {
                    const auto sessionId = currentToolContext().sessionId;
                    const Json::Value emojis = co_await fetchFavoriteEmojis(sessionId);
                    if (emojis.empty()) {
                        co_return std::string("表情库为空（QQ收藏表情列表获取失败或没有收藏表情）");
                    }
                    std::string result = "可用表情: ";
                    bool first = true;
                    for (const auto &emoji: emojis) {
                        if (!first) result += ", ";
                        result += emoji["name"].asString();
                        first = false;
                    }
                    co_return result;
                }
            }, ToolCategory::INFORMATION
        );

        // search_knowledge
        Json::Value knowledgeParams;
        knowledgeParams["type"] = "object";
        knowledgeParams["properties"]["query"]["type"] = "string";
        knowledgeParams["properties"]["query"]["description"] = "检索问题";
        knowledgeParams["required"].append("query");
        registry.registerTool(
            {
                .name = "search_knowledge",
                .description = "从知识库中检索信息。当用户问FAQ、专业知识时使用。",
                .parameters = knowledgeParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    const std::string query = args.isMember("query") ? args["query"].asString() : "";
                    if (query.empty()) {
                        co_return std::string("请提供检索问题");
                    }
                    const auto [sessionId, groupName] = currentToolContext();
                    const auto result = co_await RAGFlowClient::searchKnowledge(query, 3, sessionId);
                    co_return result.value_or("知识库检索失败");
                }
            }, ToolCategory::INFORMATION
        );

        // recall_memory
        Json::Value memoryParams;
        memoryParams["type"] = "object";
        memoryParams["properties"]["query"]["type"] = "string";
        memoryParams["properties"]["query"]["description"] = "要回忆的内容关键词，如某人的喜好、某群的习惯等";
        memoryParams["required"].append("query");
        registry.registerTool(
            {
                .name = "recall_memory",
                .description = "从长期记忆库中回忆信息（如果需要可以先获取当前群聊的名称）。当想不起某人喜好、某群习惯、过去的约定时使用。模拟人类回忆过程。",
                .parameters = memoryParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    const std::string query = args.isMember("query") ? args["query"].asString() : "";
                    if (query.empty()) co_return std::string("请提供回忆关键词");

                    const auto [sessionId, groupName] = currentToolContext();
                    const auto result =
                            co_await RAGFlowClient::searchMemory(query, 3, sessionId);
                    if (!result || result->empty()) {
                        co_return "想不起来了，没有找到相关记忆";
                    }
                    co_return "回忆起：" + result.value();
                }
            }, ToolCategory::INFORMATION
        );

        // get_group_name
        registry.registerTool(
            {
                .name = "get_group_name",
                .description = "获取当前群聊的名称。当需要知道群名或确认当前群时使用。",
                .parameters = Json::Value(),
                .handler = [](const Json::Value &) -> drogon::Task<std::string> {
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
                }
            }, ToolCategory::INFORMATION
        );

        // ========== 动作工具 ==========
        // send_face
        Json::Value faceParams;
        faceParams["type"] = "object";
        faceParams["properties"]["id"]["type"] = "integer";
        faceParams["properties"]["id"]["description"] =
                "表情ID，常用: 1-发呆, 2-撇嘴, 3-色, 4-发呆, 5-得意, 6-流泪, 7-害羞, 8-闭嘴, 9-睡, 10-大哭, 11-尴尬, 12-发怒, 13-调皮, 14-呲牙, 15-惊讶, 16-难过, 17-酷, 18-冷汗, 19-抓狂, 20-吐, 21-偷笑, 22-可爱, 23-白眼, 24-傲慢, 25-饥饿, 26-困, 27-惊恐, 28-流汗, 29-憨笑, 30-大兵, 31-奋斗, 32-咒骂, 33-疑问, 34-嘘, 35-晕, 36-折磨, 37-衰, 38-骷髅, 39-敲打, 40-再见";
        faceParams["required"].append("id");
        registry.registerTool(
            {
                .name = "send_face",
                .description = "获取QQ原生表情的CQ码。返回的CQ码必须复制到reply的content中。",
                .parameters = faceParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    int id = args.isMember("id") ? args["id"].asInt() : 1;
                    co_return fmt::format("[CQ:face,id={}]", id);
                }
            }, ToolCategory::ACTION
        );

        // send_image
        Json::Value imageParams;
        imageParams["type"] = "object";
        imageParams["properties"]["url"]["type"] = "string";
        imageParams["properties"]["url"]["description"] = "图片URL地址";
        imageParams["required"].append("url");
        registry.registerTool(
            {
                .name = "send_image",
                .description = "获取网络图片的CQ码。提供图片URL。返回的CQ码必须复制到reply的content中。",
                .parameters = imageParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    std::string url = args.isMember("url") ? args["url"].asString() : "";
                    if (url.empty()) co_return std::string("请提供图片URL");
                    co_return fmt::format("[CQ:image,file={}]", url);
                }
            }, ToolCategory::ACTION
        );

        // send_sticker
        Json::Value stickerParams;
        stickerParams["type"] = "object";
        stickerParams["properties"]["name"]["type"] = "string";
        stickerParams["properties"]["name"]["description"] = "表情名称（先调list_stickers查看可用名称）";
        stickerParams["required"].append("name");
        registry.registerTool(
            {
                .name = "send_sticker",
                .description = "获取QQ收藏表情的CQ码。先调list_stickers查看可用表情名，再用此工具。返回的CQ码必须复制到reply的content中，否则表情不会显示。",
                .parameters = stickerParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    std::string name = args.isMember("name") ? args["name"].asString() : "";
                    if (name.empty()) co_return std::string("请提供表情名称");

                    const auto sessionId = currentToolContext().sessionId;
                    Json::Value emoji = co_await findFavoriteEmoji(name, sessionId);
                    if (emoji.isNull()) {
                        co_return fmt::format("表情'{}'不存在，先调list_stickers查看可用表情", name);
                    }

                    // 商城表情（字段齐全）走 mface；个人收藏表情走 image+sub_type=1（QQ 渲染为表情）
                    if (emoji.get("is_mark_face", false).asBool()
                        && !emoji["emoji_id"].asString().empty()
                        && !emoji["key"].asString().empty()) {
                        co_return fmt::format(
                            "[CQ:mface,summary={},emoji_id={},emoji_package_id={},key={}]",
                            emoji["summary"].asString(),
                            emoji["emoji_id"].asString(),
                            emoji["emoji_package_id"].asString(),
                            emoji["key"].asString());
                    }

                    if (emoji["url"].asString().empty()) {
                        co_return fmt::format("表情'{}'缺少图片地址，无法发送", name);
                    }
                    co_return fmt::format(
                        "[CQ:image,file={},sub_type=1,summary={}]",
                        emoji["url"].asString(),
                        emoji["summary"].asString());
                }
            }, ToolCategory::ACTION
        );

        // save_sticker - 保存别人发的表情为QQ收藏表情
        Json::Value saveParams;
        saveParams["type"] = "object";
        saveParams["properties"]["file"]["type"] = "string";
        saveParams["properties"]["file"]["description"] =
                "图片在QQ缓存中的文件名（来自聊天记录JSON的images[].file字段）";
        saveParams["properties"]["url"]["type"] = "string";
        saveParams["properties"]["url"]["description"] =
                "图片URL（来自聊天记录JSON的images[].url字段），file方式获取失败时用于下载，最好同时提供";
        saveParams["properties"]["name"]["type"] = "string";
        saveParams["properties"]["name"]["description"] =
                "给表情起的简短名字（根据图片内容），如: 摸头、猫猫惊讶";
        saveParams["required"].append("file");
        saveParams["required"].append("name");
        registry.registerTool(
            {
                .name = "save_sticker",
                .description =
                "把用户发的表情/图片保存为自己的QQ收藏表情并设置描述名称。仅在用户明确要求保存表情时使用。聊天记录中图片消息会带images数组，同时传images[].file和images[].url作为参数。name必须起一个能体现图片内容的名字，方便以后用send_sticker引用。",
                .parameters = saveParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    const auto sessionId = currentToolContext().sessionId;
                    std::string file = args.isMember("file") ? args["file"].asString() : "";
                    std::string url = args.isMember("url") ? args["url"].asString() : "";
                    std::string name = args.isMember("name") ? args["name"].asString() : "";
                    if (file.empty()) co_return std::string("请提供图片文件名(file)");
                    if (name.empty()) co_return std::string("请提供表情名称(name)");

                    spdlog::info("[Sticker] save_sticker 参数: file={} url={}",
                                 file, url.substr(0, 150));

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
                    invalidateFavoriteEmojiCache();
                    std::set<std::string> beforeIds;
                    for (const auto &e: co_await fetchFavoriteEmojis(sessionId)) {
                        if (!e["res_id"].asString().empty()) {
                            beforeIds.insert(e["res_id"].asString());
                        }
                    }

                    // Step 4: add_custom_face 保存为收藏表情
                    if (!co_await OneBotClient::addCustomFace(containerPath, sessionId)) {
                        co_return std::string("保存为收藏表情失败");
                    }

                    // Step 5: 定位新表情并设置描述
                    invalidateFavoriteEmojiCache();
                    Json::Value newItem(Json::nullValue);
                    for (const auto &e: co_await fetchFavoriteEmojis(sessionId)) {
                        if (const std::string rid = e["res_id"].asString(); !rid.empty() && !beforeIds.contains(rid)) {
                            newItem = e;
                            break;
                        }
                    }
                    if (!newItem.isNull()) {
                        if (co_await OneBotClient::setCustomFaceDesc(
                                newItem["emoji_id"].asString().empty() ? "0" : newItem["emoji_id"].asString(),
                                newItem["res_id"].asString(), newItem["md5"].asString(), name, sessionId)) {
                            invalidateFavoriteEmojiCache();
                        } else {
                            spdlog::warn("[Sticker] 设置表情描述失败: {}",
                                         newItem["res_id"].asString());
                        }
                    }

                    spdlog::info("[Sticker] 已保存收藏表情: {} ({})", containerPath, name);
                    co_return fmt::format("已保存为收藏表情，名称: {}", name);
                }
            }, ToolCategory::ACTION
        );

        // rename_sticker - 修改收藏表情的名称/描述
        Json::Value renameParams;
        renameParams["type"] = "object";
        renameParams["properties"]["name"]["type"] = "string";
        renameParams["properties"]["name"]["description"] = "要改名的表情当前名称（先用list_stickers查看）";
        renameParams["properties"]["new_name"]["type"] = "string";
        renameParams["properties"]["new_name"]["description"] = "新名称，简短体现图片内容";
        renameParams["required"].append("name");
        renameParams["required"].append("new_name");
        registry.registerTool(
            {
                .name = "rename_sticker",
                .description =
                "修改收藏表情的名称/描述。仅在用户明确要求给表情改名时使用。先调list_stickers查看当前名称，再把新名称传给new_name。",
                .parameters = renameParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    std::string name = args.isMember("name") ? args["name"].asString() : "";
                    std::string newName = args.isMember("new_name") ? args["new_name"].asString() : "";
                    if (name.empty()) co_return std::string("请提供表情当前名称(name)");
                    if (newName.empty()) co_return std::string("请提供新名称(new_name)");

                    const auto sessionId = currentToolContext().sessionId;
                    Json::Value emoji = co_await findFavoriteEmoji(name, sessionId);
                    if (emoji.isNull()) {
                        co_return fmt::format("表情'{}'不存在，先调list_stickers查看可用表情", name);
                    }

                    if (!co_await OneBotClient::setCustomFaceDesc(
                            emoji["emoji_id"].asString().empty() ? "0" : emoji["emoji_id"].asString(),
                            emoji["res_id"].asString(), emoji["md5"].asString(), newName, sessionId)) {
                        co_return std::string("改名失败");
                    }

                    invalidateFavoriteEmojiCache();
                    spdlog::info("[Sticker] 表情改名: {} -> {}", name, newName);
                    co_return fmt::format("已改名为: {}", newName);
                }
            }, ToolCategory::ACTION
        );

        // delete_sticker - 从收藏表情中删除
        Json::Value delParams;
        delParams["type"] = "object";
        delParams["properties"]["name"]["type"] = "string";
        delParams["properties"]["name"]["description"] = "要删除的表情名称（先用list_stickers查看）";
        delParams["required"].append("name");
        registry.registerTool(
            {
                .name = "delete_sticker",
                .description =
                "从QQ收藏表情中删除表情。仅在用户明确要求删除表情时使用，删除前先确认名称无误。先调list_stickers查看名称。",
                .parameters = delParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    std::string name = args.isMember("name") ? args["name"].asString() : "";
                    if (name.empty()) co_return std::string("请提供表情名称(name)");

                    const auto sessionId = currentToolContext().sessionId;
                    Json::Value emoji = co_await findFavoriteEmoji(name, sessionId);
                    if (emoji.isNull()) {
                        co_return fmt::format("表情'{}'不存在，先调list_stickers查看可用表情", name);
                    }

                    if (!co_await OneBotClient::deleteCustomFace(emoji["res_id"].asString(), sessionId)) {
                        co_return std::string("删除失败");
                    }

                    invalidateFavoriteEmojiCache();
                    spdlog::info("[Sticker] 已删除收藏表情: {}", name);
                    co_return fmt::format("已删除表情: {}", name);
                }
            }, ToolCategory::ACTION
        );

        // at_user
        Json::Value atParams;
        atParams["type"] = "object";
        atParams["properties"]["qq"]["type"] = "string";
        atParams["properties"]["qq"]["description"] = "要@的QQ号（从聊天记录JSON的sender.qq字段获取）。使用 'all' @全体成员";
        atParams["required"].append("qq");
        registry.registerTool(
            {
                .name = "at_user",
                .description =
                "@某人。返回CQ码嵌入reply的content中。聊天记录格式为JSON：{\"sender\":{\"name\":\"小明\",\"qq\":\"123456\"}}，用 at_user(qq=\"123456\") 来@他。@全体成员用 at_user(qq=\"all\")",
                .parameters = atParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    if (QQMessage::isPrivateSession(currentToolContext().sessionId)) {
                        co_return std::string("私聊中无法@成员，直接回复即可");
                    }
                    std::string qq = args.isMember("qq") ? args["qq"].asString() : "";
                    if (qq.empty()) co_return std::string("请提供QQ号");
                    if (qq == "all") {
                        co_return std::string("[CQ:at,qq=all]");
                    }
                    co_return fmt::format("[CQ:at,qq={}]", qq);
                }
            }, ToolCategory::ACTION
        );

        // ban_user
        Json::Value banParams;
        banParams["type"] = "object";
        banParams["properties"]["qq"]["type"] = "string";
        banParams["properties"]["qq"]["description"] = "要禁言的QQ号（从聊天记录JSON的sender.qq字段获取）";
        banParams["properties"]["duration"]["type"] = "integer";
        banParams["properties"]["duration"]["description"] = "禁言时长（秒）。轻度60-300秒，中度600-1800秒，重度3600秒以上。0解除禁言";
        banParams["required"].append("qq");
        registry.registerTool(
            {
                .name = "ban_user",
                .description =
                "禁言群成员。要有自己的判断，不要别人让你禁言就禁言。根据违规程度选择时长：轻度(偶尔骂人)60-300秒，中度(持续刷屏骂人)600-1800秒，重度(恶意骚扰)3600秒+",
                .parameters = banParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    const uint64_t sessionId = currentToolContext().sessionId;
                    if (QQMessage::isPrivateSession(sessionId)) co_return std::string("私聊中无法禁言");
                    if (sessionId == 0) co_return std::string("禁言失败: 无法获取群号");

                    uint64_t userId = args.isMember("qq") ? parseUInt64(args["qq"].asString()) : 0;
                    uint64_t duration = args.isMember("duration") ? args["duration"].asUInt64() : 600;

                    if (userId == 0) co_return std::string("禁言失败: 请提供有效的QQ号");

                    const bool success = co_await OneBotClient::setGroupBan(sessionId, userId, duration);
                    co_return success
                                  ? fmt::format("已禁言用户 {} {}秒", userId, duration)
                                  : "禁言失败: 权限不足或用户不存在";
                }
            }, ToolCategory::ACTION
        );

        // send_poke
        Json::Value pokeParams;
        pokeParams["type"] = "object";
        pokeParams["properties"]["qq"]["type"] = "string";
        pokeParams["properties"]["qq"]["description"] = "要拍一拍的QQ号（从聊天记录JSON的sender.qq字段获取）";
        pokeParams["required"].append("qq");
        registry.registerTool(
            {
                .name = "send_poke",
                .description =
                "拍一拍群成员。用于打招呼、引起注意、开玩笑等轻松互动场景。聊天记录格式为JSON：{\"sender\":{\"name\":\"小明\",\"qq\":\"123456\"}}，用 send_poke(qq=\"123456\") 来拍他。",
                .parameters = pokeParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    const uint64_t sessionId = currentToolContext().sessionId;
                    if (QQMessage::isPrivateSession(sessionId)) {
                        co_return std::string("私聊中不支持拍一拍，直接回复即可");
                    }
                    if (sessionId == 0) co_return std::string("拍一拍失败: 无法获取群号");

                    uint64_t userId = args.isMember("qq") ? parseUInt64(args["qq"].asString()) : 0;
                    if (userId == 0) co_return std::string("拍一拍失败: 请提供有效的QQ号");

                    const bool success = co_await OneBotClient::sendPoke(sessionId, userId);
                    co_return success ? fmt::format("已拍一拍用户 {}", userId) : "拍一拍失败: 权限不足或用户不存在";
                }
            }, ToolCategory::ACTION
        );

        // recall_message
        Json::Value recallParams;
        recallParams["type"] = "object";
        recallParams["properties"]["message_id"]["type"] = "string";
        recallParams["properties"]["message_id"]["description"] = "要撤回的消息ID（从聊天记录JSON的message_id或reply_to字段获取）";
        recallParams["required"].append("message_id");
        registry.registerTool(
            {
                .name = "recall_message",
                .description =
                "撤回消息。当用户要求撤回某条消息时使用。聊天记录格式为JSON：{\"message_id\":\"12345\",\"reply_to\":\"67890\"}。若用户想撤回引用的消息，用 reply_to 字段的值；若想撤回某条消息本身，用 message_id 字段的值。",
                .parameters = recallParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    uint64_t messageId = args.isMember("message_id") ? parseUInt64(args["message_id"].asString()) : 0;
                    if (messageId == 0) co_return std::string("撤回失败: 请提供有效的消息ID");

                    const auto sessionId = currentToolContext().sessionId;
                    const bool success = co_await OneBotClient::deleteMsg(messageId, sessionId);
                    co_return success
                                  ? fmt::format("已撤回消息 {}", messageId)
                                  : "撤回失败: 消息可能已超过2分钟或权限不足";
                }
            }, ToolCategory::ACTION
        );

        // create_scheduled_task - 创建定时提醒任务
        Json::Value scheduleParams;
        scheduleParams["type"] = "object";
        scheduleParams["properties"]["time"]["type"] = "string";
        scheduleParams["properties"]["time"]["description"] =
                "触发时间，必须是 YYYY-MM-DD HH:MM:SS 格式的绝对时间。根据聊天记录中最新消息的 time 字段推算当前时间，"
                "把用户说的『明天6点』『一小时后』换算成完整日期时间再传入";
        scheduleParams["properties"]["content"]["type"] = "string";
        scheduleParams["properties"]["content"]["description"] =
                fmt::format("到点时留给自己（{}）的备忘说明，不是最终的回复文本：写清楚要提醒谁（带上对方昵称及sender.qq）、要做什么事、"
                            "以及创建时对话里的相关背景。到点后你会看到这段备忘并结合当时的聊天上下文自行组织回复",
                            Config::instance().botName);
        scheduleParams["required"].append("time");
        scheduleParams["required"].append("content");
        registry.registerTool(
            {
                .name = "create_scheduled_task",
                .description =
                "创建定时提醒任务。当用户明确要求在未来某个时刻提醒/通知某事时使用（如'明天6点叫我起床''两小时后提醒我开会'）。"
                "到点后会以【系统定时任务】消息回到当前会话，你再据此生成提醒回复。",
                .parameters = scheduleParams,
                .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
                    const std::string content = args.isMember("content") ? args["content"].asString() : "";
                    if (content.empty()) co_return std::string("请提供提醒内容(content)");
                    if (content.size() > 500) co_return std::string("提醒内容过长（最多500字符），请精简");

                    const auto remindTime =
                            TaskScheduler::parseTimeString(args.isMember("time") ? args["time"].asString() : "");
                    if (!remindTime) {
                        co_return std::string(
                            "无法识别时间格式，请按 YYYY-MM-DD HH:MM:SS 提供换算后的完整绝对时间");
                    }

                    const time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    if (*remindTime <= now + 10) {
                        co_return fmt::format("时间无效：必须晚于当前时间10秒以上。当前时间是 {}", currentDateTime());
                    }
                    if (*remindTime > now + 366LL * 24 * 3600) {
                        co_return std::string("时间过早远：不允许设置超过一年后的提醒");
                    }

                    const uint64_t sessionId = currentToolContext().sessionId;
                    if (sessionId == 0) co_return std::string("会话上下文缺失，无法确定提醒目标");
                    const bool isPrivateSession = QQMessage::isPrivateSession(sessionId);

                    TaskStore::ScheduledTask task;
                    task.sessionType = isPrivateSession ? "private" : "group";
                    task.targetId = isPrivateSession ? sessionId & ~QQMessage::kPrivateSessionFlag
                                                     : sessionId;
                    task.remindTime = *remindTime;
                    task.content = content;

                    try {
                        const int64_t id = TaskScheduler::instance().schedule(std::move(task));
                        const std::time_t t = *remindTime;
                        std::tm tm{};
                        localtime_r(&t, &tm);
                        char buffer[32];
                        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
                        co_return fmt::format("定时任务 #{} 已创建，将于 {} 在{}触发提醒",
                                              id, buffer, isPrivateSession ? "私聊" : "本群");
                    } catch (const std::exception &e) {
                        spdlog::error("[Scheduler] 创建定时任务入库失败: {}", e.what());
                        co_return std::string("创建定时任务失败，请稍后重试");
                    }
                }
            }, ToolCategory::ACTION
        );

        spdlog::info("ToolManager: 工具注册完成（共20个工具）");
    }

    void AgentToolManager::registerCustomTools() {
        auto &registry = ToolRegistry::instance();
        const auto &toolStore = ToolStore::instance();

        // 先清除所有已注册的自定义工具
        registry.clearAllCustomTools();


        // 只注册启用的工具
        const auto tools = toolStore.getEnabledCustomTools();
        int count = 0;

        for (const auto &tool: tools) {
            // 解析参数 JSON
            Json::Value params;
            if (!tool.parameters.empty()) {
                Json::CharReaderBuilder builder;
                const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
                std::string errors;
                reader->parse(tool.parameters.c_str(),
                              tool.parameters.c_str() + tool.parameters.size(),
                              &params, &errors);
                // 确保顶层 type 为 object（OpenAI function calling 要求）
                if (!params.isNull() && !params.isMember("type")) {
                    params["type"] = "object";
                }
            }

            // 根据执行类型注册不同的 handler
            if (tool.executorType == "python") {
                registry.registerTool(
                    {
                        .name = tool.name,
                        .description = tool.description,
                        .parameters = params,
                        .handler = [script = tool.scriptContent](const Json::Value &args) -> drogon::Task<std::string> {
                            co_return co_await executePythonTool(script, args);
                        }
                    }, ToolCategory::INFORMATION
                );
            } else if (tool.executorType == "http") {
                registry.registerTool(
                    {
                        .name = tool.name,
                        .description = tool.description,
                        .parameters = params,
                        .handler = [config = tool.executorConfig
                        ](const Json::Value &args) -> drogon::Task<std::string> {
                            co_return co_await executeHttpTool(config, args);
                        }
                    }, ToolCategory::INFORMATION
                );
            }

            registry.recordCustomTool(tool.name);
            count++;
            spdlog::info("ToolManager: 注册自定义工具 '{}' ({})", tool.name, tool.executorType);
        }

        spdlog::info("ToolManager: 自定义工具注册完成（共{}个）", count);
    }

    drogon::Task<std::string> AgentToolManager::executePythonTool(const std::string &scriptContent,
                                                                  const Json::Value &args) {
        if (scriptContent.empty()) {
            co_return std::string("脚本内容为空");
        }

        // 获取配置的Python解释器路径
        std::string pythonPath = ToolStore::instance().getCustomToolPython();

        // 构建输入参数 JSON
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        std::string inputJson = Json::writeString(writerBuilder, args);

        // 创建临时脚本文件（析构时自动删除）
        struct TempFile {
            std::string path;

            explicit TempFile(std::string p) : path(std::move(p)) {
            }

            TempFile(const TempFile &) = delete;

            TempFile & operator=(const TempFile &) = delete;

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
            void operator()(FILE *f) const noexcept { if (f) std::ignore = pclose(f); }
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

    drogon::Task<std::string> AgentToolManager::executeHttpTool(const std::string &config, const Json::Value &args) {
        // 解析配置
        Json::Value configJson;
        Json::CharReaderBuilder builder;
        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        std::string errors;

        if (!reader->parse(config.c_str(), config.c_str() + config.size(), &configJson, &errors)) {
            spdlog::error("HTTP工具配置解析失败: {}", errors);
            co_return std::string("工具配置错误");
        }

        std::string url = configJson.isMember("url") ? configJson["url"].asString() : "";
        std::string method = configJson.isMember("method") ? configJson["method"].asString() : "POST";

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
        const auto resp = co_await HttpUtil::send("[HttpTool]", baseUrl, path,
                                                  isGet ? drogon::Get : drogon::Post,
                                                  isGet ? Json::Value(Json::nullValue) : args,
                                                  "", 30.0, currentToolContext().sessionId);
        if (!resp) {
            co_return std::string("HTTP请求失败");
        }
        co_return std::string((*resp)->getBody());
    }

    namespace {
        // 收藏表情缓存（60秒TTL）
        std::mutex g_favEmojiCacheMutex;
        Json::Value g_favEmojiCache(Json::nullValue);
        std::chrono::steady_clock::time_point g_favEmojiCacheTime{};

        // CQ码参数值不能包含逗号和方括号，替换为空格
        std::string sanitizeCqParam(std::string s) {
            for (char &c: s) {
                if (c == ',' || c == '[' || c == ']') c = ' ';
            }
            return s;
        }
    }

    drogon::Task<Json::Value> AgentToolManager::fetchFavoriteEmojis(const std::optional<uint64_t> sessionId) {
        using namespace std::chrono_literals;
        {
            std::lock_guard lock(g_favEmojiCacheMutex);
            if (!g_favEmojiCache.isNull()
                && std::chrono::steady_clock::now() - g_favEmojiCacheTime < 60s) {
                co_return g_favEmojiCache;
            }
        }

        Json::Value result(Json::arrayValue);
        const Json::Value data = co_await OneBotClient::fetchCustomFaceDetail(sessionId);

        int idx = 0;
        for (const auto &item: data) {
            Json::Value emoji;
            const std::string desc = sanitizeCqParam(item.get("desc", "").asString());
            const std::string md5 = item.get("md5", "").asString();
            // 无名表情用 md5 前6位兜底（稳定且可区分），序号会随列表顺序漂移
            std::string fallback = md5.size() >= 6
                                       ? "表情" + md5.substr(0, 6)
                                       : fmt::format("表情{}", idx + 1);
            emoji["name"] = desc.empty() ? fallback : desc;
            emoji["summary"] = desc;
            emoji["emoji_id"] = item.get("eId", "").asString();
            emoji["emoji_package_id"] = item.get("epId", "").asString();
            emoji["key"] = item.get("key", "").asString();
            emoji["url"] = item.get("url", "").asString();
            emoji["md5"] = md5;
            emoji["res_id"] = item.get("resId", "").asString();
            emoji["is_mark_face"] = item.get("isMarkFace", false).asBool();
            result.append(emoji);
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

    drogon::Task<Json::Value> AgentToolManager::findFavoriteEmoji(
        const std::string &name, const std::optional<uint64_t> sessionId) {
        for (const Json::Value emojis = co_await fetchFavoriteEmojis(sessionId); const auto &emoji: emojis) {
            if (emoji["name"].asString() == name || emoji["summary"].asString() == name) {
                co_return emoji;
            }
        }
        co_return Json::Value(Json::nullValue);
    }

    void AgentToolManager::invalidateFavoriteEmojiCache() {
        std::lock_guard lock(g_favEmojiCacheMutex);
        g_favEmojiCache = Json::Value(Json::nullValue);
    }
}
