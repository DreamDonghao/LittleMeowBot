/// @file CommandHandler.hpp
/// @brief 命令处理器 - QQ 群命令解析与执行
#pragma once
#include <model/ChatRecordManager.hpp>
#include <model/QQMessage.hpp>
#include <drogon/utils/coroutine.h>
#include <string>

namespace LittleMeowBot {
    class CommandHandler{
    public:
        static CommandHandler& instance();

        bool isCommand(const QQMessage& message) const;

        drogon::Task<std::string> handleCommand(
            const QQMessage& message,
            ChatRecordManager& chatRecords) const;

    private:
        CommandHandler() = default;
    };
}
