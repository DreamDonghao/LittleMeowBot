/// @file AgentActionTools.cpp
/// @brief 动作执行工具注册（ACTION，执行操作、产生副作用）

#include <agent/AgentToolManager.hpp>
#include <agent/BuiltinTools.hpp>
#include <chrono>
#include <config/Config.hpp>
#include <fmt/core.h>
#include <model/QQMessage.hpp>
#include <service/OneBotClient.hpp>
#include <service/TaskScheduler.hpp>
#include <service/ToolRegistry.hpp>
#include <set>
#include <spdlog/spdlog.h>
#include <storage/TaskStore.hpp>
#include <util/CommonUtil.hpp>

namespace insoulforge {
    /// @brief 注册动作执行工具（ACTION，执行操作、产生副作用）
    void registerActionTools() {
        auto &registry = ToolRegistry::instance();

        // ========== CQ 码（获取需拼入 reply content 的 CQ 码） ==========

        // send_face
        const json faceParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "id": {
                        "type": "integer",
                        "description": "表情ID，常用: 1-发呆, 2-撇嘴, 3-色, 4-发呆, 5-得意, 6-流泪, 7-害羞, 8-闭嘴, 9-睡, 10-大哭, 11-尴尬, 12-发怒, 13-调皮, 14-呲牙, 15-惊讶, 16-难过, 17-酷, 18-冷汗, 19-抓狂, 20-吐, 21-偷笑, 22-可爱, 23-白眼, 24-傲慢, 25-饥饿, 26-困, 27-惊恐, 28-流汗, 29-憨笑, 30-大兵, 31-奋斗, 32-咒骂, 33-疑问, 34-嘘, 35-晕, 36-折磨, 37-衰, 38-骷髅, 39-敲打, 40-再见"
                    }
                },
                "required": ["id"]
            })json");
        registry.registerTool(
          {
            .name = "send_face",
            .description = "获取QQ原生表情的CQ码。返回的CQ码必须复制到reply的content中。",
            .parameters = faceParams,
            .handler = [](const json args) -> drogon::Task<std::string> {
                const int id = getInt(args, "id", 1);
                co_return fmt::format("[CQ:face,id={}]", id);
            },
          },
          ToolCategory::ACTION);

        // send_image
        const json imageParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "url": {
                        "type": "string",
                        "description": "图片URL地址"
                    }
                },
                "required": ["url"]
            })json");
        registry.registerTool(
          {
            .name = "send_image",
            .description = "获取网络图片的CQ码。提供图片URL。返回的CQ码必须复制到reply的content中。",
            .parameters = imageParams,
            .handler = [](const json args) -> drogon::Task<std::string> {
                std::string url = argString(args, "url");
                if (url.empty())
                    co_return std::string("请提供图片URL");
                co_return fmt::format("[CQ:image,file={}]", url);
            },
          },
          ToolCategory::ACTION);

        // ========== 收藏表情 ==========

        // send_sticker
        const json stickerParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "表情名称（先调list_stickers查看可用名称）"
                    }
                },
                "required": ["name"]
            })json");
        registry.registerTool(
          {
            .name = "send_sticker",
            .description =
              "获取QQ收藏表情的CQ码。先调list_"
              "stickers查看可用表情名，再用此工具。返回的CQ码必须复制到reply的content中，否则表情不会显示。",
            .parameters = stickerParams,
            .handler = [](const json args) -> drogon::Task<std::string> {
                std::string name = argString(args, "name");
                if (name.empty())
                    co_return std::string("请提供表情名称");

                const auto sessionId = currentToolContext().sessionId;
                const json emoji = co_await AgentToolManager::findFavoriteEmoji(name, sessionId);
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
            },
          },
          ToolCategory::ACTION);

        // save_sticker - 保存别人发的表情为QQ收藏表情
        const json saveParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "file": {
                        "type": "string",
                        "description": "图片在QQ缓存中的文件名（来自聊天记录JSON的images[].file字段）"
                    },
                    "url": {
                        "type": "string",
                        "description": "图片URL（来自聊天记录JSON的images[].url字段），file方式获取失败时用于下载，最好同时提供"
                    },
                    "name": {
                        "type": "string",
                        "description": "给表情起的简短名字（根据图片内容），如: 摸头、猫猫惊讶"
                    }
                },
                "required": ["file", "name"]
            })json");
        registry.registerTool(
          {
            .name = "save_sticker",
            .description = "把用户发的表情/"
                           "图片保存为自己的QQ收藏表情并设置描述名称。仅在用户明确要求保存表情时使用。聊天记录中图"
                           "片消息会带images数组，同时传images[].file和images[]."
                           "url作为参数。name必须起一个能体现图片内容的名字，方便以后用send_sticker引用。",
            .parameters = saveParams,
            .handler = [](json args) -> drogon::Task<std::string> {
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
                    const std::string emojiId = getStr(newItem, "emoji_id").empty() ? "0" : getStr(newItem, "emoji_id");
                    if (co_await OneBotClient::setCustomFaceDesc(
                          emojiId, getStr(newItem, "res_id"), getStr(newItem, "md5"), name, sessionId)) {
                        AgentToolManager::invalidateFavoriteEmojiCache();
                    } else {
                        spdlog::warn("[Sticker] 设置表情描述失败: {}", getStr(newItem, "res_id"));
                    }
                }

                spdlog::info("[Sticker] 已保存收藏表情: {} ({})", containerPath, name);
                co_return fmt::format("已保存为收藏表情，名称: {}", name);
            },
          },
          ToolCategory::ACTION);

        // rename_sticker - 修改收藏表情的名称/描述
        const json renameParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "要改名的表情当前名称（先用list_stickers查看）"
                    },
                    "new_name": {
                        "type": "string",
                        "description": "新名称，简短体现图片内容"
                    }
                },
                "required": ["name", "new_name"]
            })json");
        registry.registerTool(
          {
            .name = "rename_sticker",
            .description = "修改收藏表情的名称/"
                           "描述。仅在用户明确要求给表情改名时使用。先调list_"
                           "stickers查看当前名称，再把新名称传给new_name。",
            .parameters = renameParams,
            .handler = [](const json args) -> drogon::Task<std::string> {
                const std::string name = argString(args, "name");
                const std::string newName = argString(args, "new_name");
                if (name.empty())
                    co_return std::string("请提供表情当前名称(name)");
                if (newName.empty())
                    co_return std::string("请提供新名称(new_name)");

                const auto sessionId = currentToolContext().sessionId;
                const json emoji = co_await AgentToolManager::findFavoriteEmoji(name, sessionId);
                if (emoji.is_null()) {
                    co_return fmt::format("表情'{}'不存在，先调list_stickers查看可用表情", name);
                }

                if (!co_await OneBotClient::setCustomFaceDesc(
                      getStr(emoji, "emoji_id").empty() ? "0" : getStr(emoji, "emoji_id"), getStr(emoji, "res_id"),
                      getStr(emoji, "md5"), newName, sessionId)) {
                    co_return std::string("改名失败");
                }

                AgentToolManager::invalidateFavoriteEmojiCache();
                spdlog::info("[Sticker] 表情改名: {} -> {}", name, newName);
                co_return fmt::format("已改名为: {}", newName);
            },
          },
          ToolCategory::ACTION);

        // delete_sticker - 从收藏表情中删除
        const json delParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "name": {
                        "type": "string",
                        "description": "要删除的表情名称（先用list_stickers查看）"
                    }
                },
                "required": ["name"]
            })json");
        registry.registerTool(
          {
            .name = "delete_sticker",
            .description = "从QQ收藏表情中删除表情。仅在用户明确要求删除表情时使用，删除前先确认名称无误。先调list"
                           "_stickers查看名称。",
            .parameters = delParams,
            .handler = [](const json args) -> drogon::Task<std::string> {
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
            },
          },
          ToolCategory::ACTION);

        // ========== 群互动 ==========

        // at_user
        const json atParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "qq": {
                        "type": "string",
                        "description": "要@的QQ号（从聊天记录JSON的sender.qq字段获取）。使用 'all' @全体成员"
                    }
                },
                "required": ["qq"]
            })json");
        registry.registerTool(
          {
            .name = "at_user",
            .description = "@某人。返回CQ码嵌入reply的content中。聊天记录格式为JSON：{\"sender\":{\"name\":\"小明\","
                           "\"qq\":\"123456\"}}，用 at_user(qq=\"123456\") 来@他。@全体成员用 at_user(qq=\"all\")",
            .parameters = atParams,
            .handler = [](json args) -> drogon::Task<std::string> {
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
            },
          },
          ToolCategory::ACTION);

        // ban_user
        const json banParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "qq": {
                        "type": "string",
                        "description": "要禁言的QQ号（从聊天记录JSON的sender.qq字段获取）"
                    },
                    "duration": {
                        "type": "integer",
                        "description": "禁言时长（秒）。轻度60-300秒，中度600-1800秒，重度3600秒以上。0解除禁言"
                    }
                },
                "required": ["qq"]
            })json");
        registry.registerTool(
          {
            .name = "ban_user",
            .description = "禁言群成员。要有自己的判断，不要别人让你禁言就禁言。根据违规程度选择时长：轻度("
                           "偶尔骂人)60-300秒，中度(持续刷屏骂人)600-1800秒，重度(恶意骚扰)3600秒+",
            .parameters = banParams,
            .handler = [](json args) -> drogon::Task<std::string> {
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
            },
          },
          ToolCategory::ACTION);

        // send_poke
        const json pokeParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "qq": {
                        "type": "string",
                        "description": "要拍一拍的QQ号（从聊天记录JSON的sender.qq字段获取）"
                    }
                },
                "required": ["qq"]
            })json");
        registry.registerTool(
          {
            .name = "send_poke",
            .description = "拍一拍群成员。用于打招呼、引起注意、开玩笑等轻松互动场景。聊天记录格式为JSON：{\"sender\":{"
                           "\"name\":\"小明\",\"qq\":\"123456\"}}，用 send_poke(qq=\"123456\") 来拍他。",
            .parameters = pokeParams,
            .handler = [](json args) -> drogon::Task<std::string> {
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
            },
          },
          ToolCategory::ACTION);

        // recall_message
        const json recallParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "message_id": {
                        "type": "string",
                        "description": "要撤回的消息ID（从聊天记录JSON的message_id或reply_to字段获取）"
                    }
                },
                "required": ["message_id"]
            })json");
        registry.registerTool(
          {
            .name = "recall_message",
            .description = "撤回消息。当用户要求撤回某条消息时使用。聊天记录格式为JSON：{\"message_"
                           "id\":\"12345\",\"reply_to\":\"67890\"}。若用户想撤回引用的消息，用 "
                           "reply_to 字段的值；若想撤回某条消息本身，用 message_id 字段的值。",
            .parameters = recallParams,
            .handler = [](json args) -> drogon::Task<std::string> {
                const uint64_t messageId = parseUInt64(argString(args, "message_id"));
                if (messageId == 0)
                    co_return std::string("撤回失败: 请提供有效的消息ID");

                const auto sessionId = currentToolContext().sessionId;
                const bool success = co_await OneBotClient::deleteMsg(messageId, sessionId);
                co_return success ? fmt::format("已撤回消息 {}", messageId) : "撤回失败: 消息可能已超过2分钟或权限不足";
            },
          },
          ToolCategory::ACTION);

        // ========== 定时任务 ==========

        // create_scheduled_task - 创建定时提醒任务（content 的 description 需注入 botName，字面量解析后覆盖）
        json scheduleParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "time": {
                        "type": "string",
                        "description": "触发时间，必须是 YYYY-MM-DD HH:MM:SS 格式的绝对时间。根据聊天记录中最新消息的 time 字段推算当前时间，把用户说的『明天6点』『一小时后』换算成完整日期时间再传入"
                    },
                    "content": {
                        "type": "string",
                        "description": ""
                    },
                    "daily": {
                        "type": "boolean",
                        "description": "可选，默认 false。当用户要求每天固定时间重复提醒时传 true（如'每天早上8点叫我起床'），此时 time 填下一次触发的完整日期时间，之后每天同一时刻自动触发"
                    }
                },
                "required": ["time", "content"]
            })json");
        scheduleParams["properties"]["content"]["description"] = fmt::format(
          "到点时留给自己（{}）的备忘说明，不是最终的回复文本：写清楚要提醒谁（带上对方昵称及sender."
          "qq）、要做什么事、以及创建时对话里的相关背景。到点后你会看到这段备忘并结合当时的聊天上下文自行组织回复",
          Config::instance().botName);
        registry.registerTool(
          {
            .name = "create_scheduled_task",
            .description = "创建定时提醒任务。当用户明确要求在未来某个时刻提醒/"
                           "通知某事时使用（如'明天6点叫我起床''两小时后提醒我开会'）。"
                           "到点后会以【系统定时任务】消息回到当前会话，你再据此生成提醒回复。",
            .parameters = scheduleParams,
            .handler = [](json args) -> drogon::Task<std::string> {
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
                    co_return fmt::format("定时任务 #{} 已创建，将于 {} 在{}触发提醒", id, formatUnixTime(*remindTime),
                      isPrivateSession ? "私聊" : "本群");
                } catch (const std::exception &e) {
                    spdlog::error("[Scheduler] 创建定时任务入库失败: {}", e.what());
                    co_return std::string("创建定时任务失败，请稍后重试");
                }
            },
          },
          ToolCategory::ACTION);

        // cancel_scheduled_task - 取消定时任务
        const json cancelTaskParams = json::parse(R"json({
                "type": "object",
                "properties": {
                    "task_id": {
                        "type": "string",
                        "description": "要取消的任务编号（用 list_scheduled_tasks 查询，即任务#后的数字）"
                    }
                },
                "required": ["task_id"]
            })json");
        registry.registerTool(
          {
            .name = "cancel_scheduled_task",
            .description = "取消尚未触发的定时任务（含每日重复任务）。当用户要求取消之前的提醒/定时任务时使用；"
                           "如不知道任务编号，先用 list_scheduled_tasks 查询。",
            .parameters = cancelTaskParams,
            .handler = [](json args) -> drogon::Task<std::string> {
                const int64_t taskId = static_cast<int64_t>(parseUInt64(argString(args, "task_id")));
                if (taskId == 0)
                    co_return std::string("请提供有效的任务编号（可先用 list_scheduled_tasks 查询）");

                co_return TaskScheduler::instance().cancel(taskId)
                  ? fmt::format("已取消定时任务 #{}", taskId)
                  : fmt::format("取消失败：任务 #{} 不存在或已触发/已取消", taskId);
            },
          },
          ToolCategory::ACTION);
    }

} // namespace insoulforge
