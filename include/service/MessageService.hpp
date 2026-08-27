/// @file MessageService.hpp
/// @brief QQ 消息服务 - 消息发送与处理
/// @author donghao
/// @date 2026-04-02
/// @details 封装 QQ 消息的发送和处理逻辑：
///          - 群消息发送：sendGroupMsg()
///          - @格式转换：convertAtToCQCode()
///          - 禁言管理：setGroupBan()
///          - 群信息获取：getGroupInfo()

#pragma once
#include <model/ChatRecordManager.hpp>
#include <model/QQMessage.hpp>
#include <drogon/utils/coroutine.h>
#include <json/value.h>
#include <optional>
#include <string>

/// @brief 消息服务 - 封装 QQ 消息发送逻辑，对接 OneBot API
namespace LittleMeowBot::MessageService {
    /// @brief 将文本中的@格式转换为 CQ 码
    /// @param text 原始文本（可能包含 @昵称 或 @[QQ:xxx] 格式）
    /// @return 转换后的文本（包含 [CQ:at,qq=xxx] 格式）
    std::string convertAtToCQCode(const std::string &text);

    /// @brief 发送群消息
    /// @param groupId 群号
    /// @param message 消息内容
    /// @param chatRecords 聊天记录管理器（用于更新记录）
    drogon::Task<> sendGroupMsg(
        Json::UInt64 groupId,
        const std::string &message,
        const ChatRecordManager &chatRecords);

    /// @brief 发送私聊消息
    /// @param userId 用户QQ号
    /// @param message 消息内容
    /// @param chatRecords 聊天记录管理器（用于更新记录）
    drogon::Task<> sendPrivateMsg(
        Json::UInt64 userId,
        const std::string &message,
        const ChatRecordManager &chatRecords);

    /// @brief 禁言群成员
    /// @param groupId 群号
    /// @param userId 用户QQ号
    /// @param duration 禁言时长（秒），0表示解除禁言
    /// @return 是否成功
    [[nodiscard]] drogon::Task<bool> setGroupBan(Json::UInt64 groupId, Json::UInt64 userId, Json::UInt64 duration);

    /// @brief 获取群信息
    /// @param groupId 群号
    /// @return 群信息JSON（包含group_name等）
    [[nodiscard]] drogon::Task<Json::Value> getGroupInfo(Json::UInt64 groupId);

    /// @brief 获取并更新会话名称（群聊为群名，私聊为 QQ 昵称）
    /// @param sessionId 会话 ID（私聊带标志位）
    /// @return 会话名称
    [[nodiscard]] drogon::Task<std::string> fetchAndUpdateSessionName(Json::UInt64 sessionId);

    /// @brief 拍一拍群成员
    /// @param groupId 群号
    /// @param userId 用户QQ号
    /// @return 是否成功
    [[nodiscard]] drogon::Task<bool> setGroupPoke(Json::UInt64 groupId, Json::UInt64 userId);

    /// @brief 撤回消息
    /// @param messageId 消息ID
    /// @return 是否成功
    [[nodiscard]] drogon::Task<bool> deleteMessage(Json::UInt64 messageId,
                                                   std::optional<uint64_t> groupId = std::nullopt);
}
