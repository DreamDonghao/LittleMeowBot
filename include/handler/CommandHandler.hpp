/// @file CommandHandler.hpp
/// @brief 命令处理器 - QQ 群命令解析与执行
#pragma once
#include <model/ChatRecordManager.hpp>
#include <model/QQMessage.hpp>
#include <drogon/utils/coroutine.h>
#include <string>

namespace LittleMeowBot {
    [[nodiscard]] bool isCommand(const QQMessage &message);

    drogon::Task<std::string> handleCommand(
        const QQMessage &message,
        ChatRecordManager &chatRecords);
}
