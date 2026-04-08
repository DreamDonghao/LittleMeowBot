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
#include <string>

namespace LittleMeowBot {
    /// @brief 消息服务类（单例模式）
    /// @details 封装 QQ 消息发送逻辑，对接 OneBot API
    class MessageService{
    public:
        /// @brief 获取单例实例
        /// @return MessageService 实例引用
        static MessageService& instance();

        /// @brief 将文本中的@格式转换为 CQ 码
        /// @param text 原始文本（可能包含 @昵称 或 @[QQ:xxx] 格式）
        /// @return 转换后的文本（包含 [CQ:at,qq=xxx] 格式）
        static std::string convertAtToCQCode(const std::string& text);

        /// @brief 发送群消息
        /// @param groupId 群号
        /// @param message 消息内容
        /// @param chatRecords 聊天记录管理器（用于更新记录）
        drogon::Task<> sendGroupMsg(
            Json::UInt64 groupId,
            const std::string& message,
            const ChatRecordManager& chatRecords) const;

        /// @brief 禁言群成员
        /// @param groupId 群号
        /// @param userId 用户QQ号
        /// @param duration 禁言时长（秒），0表示解除禁言
        /// @return 是否成功
        drogon::Task<bool> setGroupBan(
            Json::UInt64 groupId,
            Json::UInt64 userId,
            Json::UInt64 duration) const;

        /// @brief 获取群信息
        /// @param groupId 群号
        /// @return 群信息JSON（包含group_name等）
        drogon::Task<Json::Value> getGroupInfo(Json::UInt64 groupId) const;

        /// @brief 获取并更新群名称
        /// @param groupId 群号
        /// @return 群名称
        drogon::Task<std::string> fetchAndUpdateGroupName(Json::UInt64 groupId) const;

    private:
        MessageService() = default;
    };
}
