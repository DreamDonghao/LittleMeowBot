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
#include <string>
#include <unordered_map>
#include <utility>

#include <util/JsonUtil.hpp>

namespace insoulforge {
    /// @brief QQ 消息模型类
    /// @details 封装单条 QQ 群消息的解析和格式化
    class QQMessage {
    public:
        /// @brief 构造函数，从 JSON 解析消息
        /// @param qqMessageJson OneBot 消息 JSON
        explicit QQMessage(json qqMessageJson);

        /// @brief 检查是否 @ 了机器人
        /// @return 是否 @ 了机器人
        [[nodiscard]] bool atMe() const;

        /// @brief 检查是否高优先级消息：@机器人、私聊或系统定时任务触发。
        /// 此类消息在并发控制中不允许被静默丢弃，而是取消当前处理并等待接管
        /// （AgentSystem 抢占判断与 Router 硬规则共用该语义）
        /// @return 是否高优先级
        [[nodiscard]] bool isPriorityMessage() const;

        /// @brief 获取群号
        /// @return 群号
        [[nodiscard]] uint64_t getGroupId() const;

        /// @brief 私聊会话标志位：私聊会话 ID = 用户QQ号 | kPrivateSessionFlag，
        /// 与群号共享同一 uint64 键空间（群号不会用到最高位），下游存储/Map 无需区分
        static constexpr uint64_t kPrivateSessionFlag = 1ULL << 63;

        /// @brief 系统定时任务虚拟账号：现网不存在的保留 QQ 号。
        /// 调度器用它作为 sender 合成【系统定时任务】消息注入消息接口，Router 据此确定性放行
        static constexpr uint64_t kSystemAccountId = 10000000000ULL;

        /// @brief 判断会话 ID 是否为私聊会话
        /// @param sessionId 会话 ID
        /// @return 是否私聊
        [[nodiscard]] static constexpr bool isPrivateSession(const uint64_t sessionId) {
            return (sessionId & kPrivateSessionFlag) != 0;
        }

        /// @brief 将会话 ID 解析为存储用的会话类型与目标 ID（定时任务等按此维度落库）
        /// @param sessionId 会话 ID（私聊带 kPrivateSessionFlag）
        /// @return {sessionType("group"|"private"), targetId(群号或未加标志位的 QQ 号)}
        [[nodiscard]] static std::pair<std::string, uint64_t> parseSessionTarget(const uint64_t sessionId) {
            return {isPrivateSession(sessionId) ? "private" : "group",
              isPrivateSession(sessionId) ? sessionId & ~kPrivateSessionFlag : sessionId};
        }

        /// @brief 是否私聊消息
        /// @return OneBot message_type == "private"
        [[nodiscard]] bool isPrivate() const;

        /// @brief 获取会话 ID（群聊=群号；私聊=用户QQ号|kPrivateSessionFlag）
        /// @return 会话 ID，可作下游存储与并发控制的统一键
        [[nodiscard]] uint64_t getSessionId() const;

        /// @brief 获取机器人自己的 QQ 号
        /// @return 机器人 QQ 号
        [[nodiscard]] uint64_t getSelfQQNumber() const;

        /// @brief 获取发送者 QQ 号
        /// @return 发送者 QQ 号
        [[nodiscard]] uint64_t getSenderQQNumber() const;

        /// @brief 获取消息归属用户的 QQ 号（顶层 user_id 字段，缺失时回退 sender）。
        /// 私聊会话以该字段为准：定时任务合成的私聊事件中 sender 是系统账号，
        /// 会话与回复目标必须指向真实的用户 QQ
        /// @return 用户 QQ 号
        [[nodiscard]] uint64_t getUserId() const;

        /// @brief 获取消息 ID
        /// @return 消息 ID
        [[nodiscard]] uint64_t getMessageId() const;

        /// @brief 格式化消息（异步，可能需要识别图片）
        /// @details 将消息转换为JSON格式，包含发送者、消息ID、内容等
        drogon::Task<> formatMessage();

        /// @brief 获取格式化后的消息（JSON格式）
        /// @return 格式化后的JSON字符串
        [[nodiscard]] std::string getFormatMessage() const;

        /// @brief 获取原始消息文本（不含 CQ 码）
        /// @return 原始消息文本
        [[nodiscard]] std::string getRawMessage() const;

        /// @brief 设置自定义 QQ 昵称
        /// @param qqNumber QQ 号
        /// @param qqName 自定义昵称
        static void setCustomQQName(uint64_t qqNumber, const std::string &qqName);

        /// @brief 获取 QQ 昵称
        /// @param qqNumber QQ 号
        /// @return 昵称（优先返回自定义昵称）
        static std::string getQQName(uint64_t qqNumber);

        /// @brief 获取昵称到QQ号的反向映射（用于@转换）
        /// @return 昵称到QQ号的映射表
        static std::unordered_map<std::string, uint64_t> getNameToQQMap();

    private:
        /// @brief 获取发送者昵称
        /// @return 发送者昵称
        [[nodiscard]] std::string getSenderQQName() const;

        const json m_qqMessageJson; ///< OneBot 消息 JSON
        std::string m_formatMessage; ///< 格式化后的消息（JSON格式）
        uint64_t m_replyTo{0}; ///< 引用的消息ID
        bool m_isAtMe{false}; ///< 是否 @ 了机器人

        inline static std::unordered_map<uint64_t, std::string> m_QQNameMap; ///< QQ 号到昵称映射
        inline static std::unordered_map<uint64_t, std::string> m_customQQNameMap; ///< 自定义昵称映射
    };
} // namespace insoulforge
