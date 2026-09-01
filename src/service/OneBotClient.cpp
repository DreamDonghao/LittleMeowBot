/// @file OneBotClient.cpp
/// @brief OneBot HTTP API 客户端 - 实现

#include <config/Config.hpp>
#include <service/OneBotClient.hpp>
#include <spdlog/spdlog.h>
#include <util/CommonUtil.hpp>
#include <util/HttpUtil.hpp>
#include <util/Logger.hpp>

namespace insoulforge::OneBotClient {
    namespace {
        /// @brief 通用 API 调用
        /// @param tag 日志前缀，如 "[Msg]"、"[Sticker]"
        /// @param api API 名称，如 "send_group_msg"（内部补 "/" 前缀）
        /// @param params 请求参数（JSON body）
        /// @param sessionId 会话 ID（用于会话日志，可空）
        /// @param timeout 超时秒数
        /// @return 响应 JSON（含 status/retcode/data）；HTTP 非 200 或 status != ok 时返回 nullopt（已记日志）
        [[nodiscard]] drogon::Task<std::optional<Json::Value>> callApi(std::string_view tag, const std::string &api,
          const Json::Value &params, std::optional<uint64_t> sessionId = std::nullopt, double timeout = 30.0) {
            const auto &config = Config::instance();
            const auto resp = co_await HttpUtil::send(
              tag, config.qqHttpHost, "/" + api, drogon::Post, params, config.accessToken, timeout, sessionId);
            if (!resp) {
                co_return std::nullopt;
            }
            const auto json = (*resp)->getJsonObject();
            if ((*resp)->getStatusCode() != drogon::k200OK || !json) {
                Logger::session(sessionId.value_or(0))
                  .error(
                    "{} OneBot API {} 请求失败: http_status={}", tag, api, static_cast<int>((*resp)->getStatusCode()));
                co_return std::nullopt;
            }
            if (json->get("status", "failed").asString() != "ok") {
                Logger::session(sessionId.value_or(0))
                  .error("{} OneBot API {} 失败: status={}, retcode={}", tag, api, json->get("status", "").asString(),
                    json->get("retcode", -1).asInt());
                co_return std::nullopt;
            }
            co_return *json;
        }

        /// @brief 发送消息的公共实现（群聊/私聊共用，仅 API 名与目标字段不同）
        drogon::Task<std::optional<uint64_t>> sendMessage(const std::string &api, const std::string &targetKey,
          const Json::UInt64 targetId, const std::string &message, const std::optional<uint64_t> sessionId) {
            Json::Value params;
            params[targetKey] = targetId;
            params["message"] = message;
            params["auto_escape"] = false;

            const auto resp = co_await callApi("[Msg]", api, params, sessionId);
            if (!resp) {
                Logger::session(sessionId.value_or(0))
                  .error("发送消息错误: msgLen={}, preview={}", message.size(), message.substr(0, 200));
                co_return std::nullopt;
            }
            co_return jsonToUInt64((*resp)["data"]["message_id"]);
        }
    } // namespace

    drogon::Task<std::optional<uint64_t>> sendGroupMsg(const uint64_t groupId, const std::string &message) {
        co_return co_await sendMessage("send_group_msg", "group_id", groupId, message, groupId);
    }

    drogon::Task<std::optional<uint64_t>> sendPrivateMsg(
      const uint64_t userId, const std::string &message, const std::optional<uint64_t> sessionId) {
        co_return co_await sendMessage("send_private_msg", "user_id", userId, message, sessionId);
    }

    drogon::Task<bool> setGroupBan(const uint64_t groupId, const uint64_t userId, const uint64_t duration) {
        Json::Value params;
        params["group_id"] = groupId;
        params["user_id"] = userId;
        params["duration"] = duration;

        const auto resp = co_await callApi("[Ban]", "set_group_ban", params, groupId);
        if (!resp) {
            co_return false;
        }
        Logger::session(groupId).info("禁言成功: 用户{} 时长{}秒", userId, duration);
        co_return true;
    }

    drogon::Task<Json::Value> getGroupInfo(const uint64_t groupId) {
        Json::Value params;
        params["group_id"] = groupId;

        const auto resp = co_await callApi("[GroupInfo]", "get_group_info", params, groupId);
        co_return resp.value_or(Json::Value(Json::nullValue));
    }

    drogon::Task<Json::Value> getStrangerInfo(const uint64_t userId, const std::optional<uint64_t> sessionId) {
        Json::Value params;
        params["user_id"] = userId;

        const auto resp = co_await callApi("[StrangerInfo]", "get_stranger_info", params, sessionId);
        co_return resp.value_or(Json::Value(Json::nullValue));
    }

    drogon::Task<bool> sendPoke(const uint64_t groupId, const uint64_t userId) {
        Json::Value params;
        params["group_id"] = groupId;
        params["user_id"] = userId;

        const auto resp = co_await callApi("[Poke]", "send_poke", params, groupId);
        if (!resp) {
            co_return false;
        }
        Logger::session(groupId).info("拍一拍成功: 用户{}", userId);
        co_return true;
    }

    drogon::Task<bool> deleteMsg(const uint64_t messageId, const std::optional<uint64_t> sessionId) {
        Json::Value params;
        params["message_id"] = messageId;

        const auto resp = co_await callApi("[Recall]", "delete_msg", params, sessionId);
        if (!resp) {
            co_return false;
        }
        Logger::session(sessionId.value_or(0)).info("撤回消息成功: message_id={}", messageId);
        co_return true;
    }

    drogon::Task<std::optional<std::string>> getImage(
      const std::string &file, const std::optional<uint64_t> sessionId) {
        Json::Value params;
        params["file"] = file;

        const auto resp = co_await callApi("[Sticker]", "get_image", params, sessionId, 15.0);
        if (!resp) {
            co_return std::nullopt;
        }
        co_return (*resp)["data"]["file"].asString();
    }

    drogon::Task<std::optional<std::string>> downloadFile(
      const std::string &url, const std::optional<uint64_t> sessionId) {
        Json::Value params;
        params["url"] = url;

        const auto resp = co_await callApi("[Sticker]", "download_file", params, sessionId);
        if (!resp) {
            co_return std::nullopt;
        }
        co_return (*resp)["data"]["file"].asString();
    }

    drogon::Task<bool> addCustomFace(const std::string &file, const std::optional<uint64_t> sessionId) {
        Json::Value params;
        params["file"] = file;

        const auto resp = co_await callApi("[Sticker]", "add_custom_face", params, sessionId);
        if (!resp) {
            spdlog::error("[Sticker] 保存收藏表情失败: {}", file);
            co_return false;
        }
        co_return true;
    }

    drogon::Task<bool> setCustomFaceDesc(const std::string &emojiId, const std::string &resId, const std::string &md5,
      const std::string &desc, const std::optional<uint64_t> sessionId) {
        Json::Value params;
        params["emoji_id"] = emojiId;
        params["res_id"] = resId;
        params["md5"] = md5;
        params["desc"] = desc;

        const auto resp = co_await callApi("[Sticker]", "set_custom_face_desc", params, sessionId);
        co_return resp.has_value();
    }

    drogon::Task<bool> deleteCustomFace(const std::string &resId, const std::optional<uint64_t> sessionId) {
        Json::Value params;
        params["res_id"] = resId;

        const auto resp = co_await callApi("[Sticker]", "delete_custom_face", params, sessionId);
        co_return resp.has_value();
    }

    drogon::Task<Json::Value> fetchCustomFaceDetail(const std::optional<uint64_t> sessionId) {
        Json::Value params;
        params["count"] = 200;

        const auto resp = co_await callApi("[Sticker]", "fetch_custom_face_detail", params, sessionId);
        if (!resp) {
            co_return Json::Value(Json::arrayValue);
        }
        co_return (*resp)["data"];
    }
} // namespace insoulforge::OneBotClient
