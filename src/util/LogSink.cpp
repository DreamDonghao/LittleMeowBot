/// @file LogSink.cpp
/// @brief 将 spdlog 日志转发到内存查询服务 - 实现

#include <util/LogBuffer.hpp>
#include <util/LogSink.hpp>

namespace insoulforge {
    void LogSink::sink_it_(const spdlog::details::log_msg &message) { LogBuffer::instance().append(message); }

    void LogSink::flush_() {}
} // namespace insoulforge
