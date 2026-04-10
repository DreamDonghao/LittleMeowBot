/// @file QQMessage.hpp
/// @brief QQ 消息模型 - 消息解析与格式化
/// @author donghao
/// @date 2026-04-02
/// @details 解析和处理 QQ 群消息，支持：
///          - 消息格式化（日期、用户名、@提及、图片识别）
///          - CQ 码解析（文本、表情、图片、回复引用）
///          - 用户昵称管理（自定义昵称映射）

#pragma once

#include <drogon/drogon.h>
#include <unordered_map>
#include <string>

namespace LittleMeowBot {
    /// @brief QQ 消息模型类
    /// @details 封装单条 QQ 群消息的解析和格式化
    class QQMessage{
    public:
        /// @brief 构造函数，从 JSON 解析消息
        /// @param qqMessageJson OneBot 消息 JSON
        explicit QQMessage(const Json::Value& qqMessageJson);

        /// @brief 设置消息 JSON（重新解析）
        /// @param qqMessageJson OneBot 消息 JSON
        void setMessageJson(const Json::Value& qqMessageJson);

        /// @brief 检查是否 @ 了机器人
        /// @return 是否 @ 了机器人
        [[nodiscard]] bool atMe() const;

        /// @brief 检查消息是否包含图片
        /// @return 是否包含图片
        [[nodiscard]] bool existImage() const;

        /// @brief 获取群号
        /// @return 群号
        [[nodiscard]] Json::UInt64 getGroupId() const;

        /// @brief 获取机器人自己的 QQ 号
        /// @return 机器人 QQ 号
        [[nodiscard]] Json::UInt64 getSelfQQNumber() const;

        /// @brief 获取发送者 QQ 号
        /// @return 发送者 QQ 号
        [[nodiscard]] Json::UInt64 getSenderQQNumber() const;

        /// @brief 获取发送者昵称
        /// @return 发送者昵称
        [[nodiscard]] Json::String getSenderQQName() const;

        /// @brief 获取发送者群名片
        /// @return 群名片（若无则返回空）
        [[nodiscard]] Json::String getSenderGroupName() const;

        /// @brief 获取消息 ID
        /// @return 消息 ID
        [[nodiscard]] Json::UInt64 getMessageId() const;

        /// @brief 格式化消息（异步，可能需要识别图片）
        /// @details 将消息转换为JSON格式，包含发送者、消息ID、内容等
        drogon::Task<> formatMessage();

        /// @brief 获取格式化后的消息（JSON格式）
        /// @return 格式化后的JSON字符串
        [[nodiscard]] Json::String getFormatMessage() const;

        /// @brief 获取引用的消息ID
        /// @return 引用的消息ID，无引用返回0
        [[nodiscard]] uint64_t getReplyTo() const;

        /// @brief 获取原始消息文本（不含 CQ 码）
        /// @return 原始消息文本
        [[nodiscard]] std::string getRawMessage() const;

        /// @brief 设置自定义 QQ 昵称
        /// @param qqNumber QQ 号
        /// @param qqName 自定义昵称
        static void setCustomQQName(Json::UInt64 qqNumber, const Json::String& qqName);

        /// @brief 获取 QQ 昵称
        /// @param qqNumber QQ 号
        /// @return 昵称（优先返回自定义昵称）
        static Json::String getQQName(Json::UInt64 qqNumber);

        /// @brief 获取昵称到QQ号的反向映射（用于@转换）
        /// @return 昵称到QQ号的映射表
        static std::unordered_map<std::string, uint64_t> getNameToQQMap();

        /// @brief 添加消息缓存
        /// @param messageId 消息 ID
        /// @param message 格式化后的消息
        static void addMessageCache(Json::UInt64 messageId, Json::String message);

    private:
        const Json::Value* m_qqMessageJson{}; ///< OneBot 消息 JSON 指针
        Json::String m_formatMessage;         ///< 格式化后的消息（JSON格式）
        uint64_t m_replyTo{0};                ///< 引用的消息ID
        bool m_isAtMe{false};                 ///< 是否 @ 了机器人
        bool m_isExistImage{false};           ///< 是否包含图片


        inline static std::unordered_map<Json::UInt64, Json::String> m_QQNameMap;       ///< QQ 号到昵称映射
        inline static std::unordered_map<Json::UInt64, Json::String> m_customQQNameMap; ///< 自定义昵称映射
        inline static std::unordered_map<uint64_t, Json::String> m_messageCache;        ///< 消息缓存
    };
}
