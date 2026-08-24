/// @file LogSink.cpp
/// @brief 将 spdlog 日志转发到内存查询服务 - 实现

#include <util/LogSink.hpp>
#include <util/LogBuffer.hpp>

namespace LittleMeowBot {
    void LogSink::sink_it_(const spdlog::details::log_msg& message) {
        LogBuffer::instance().append(message);
    }

    void LogSink::flush_() {
    }
}