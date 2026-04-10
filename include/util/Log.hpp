/// @file Log.hpp
/// @brief 日志工具 - 基于 spdlog 封装
/// @author donghao
/// @date 2026-04-13

#pragma once

#include <format>
#include <string_view>
#include <string>
#include <memory>
#include <vector>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/async.h>

#ifdef ERROR
#undef ERROR
#endif

namespace LittleMeowBot {
    class Log{
    public:
        using Level = spdlog::level::level_enum;

        static void init(bool enableFile = false, const std::string& logPath = "logs/bot.log", bool asyncMode = true);
        static void shutdown();
        static void setLevel(Level level);
        static void flush();

        template <typename... Args>
        static void trace(const std::string_view f, const Args&... args){
            logImpl(Level::trace, std::vformat(std::string(f), std::make_format_args(args...)));
        }

        template <typename... Args>
        static void debug(const std::string_view f, const Args&... args){
            logImpl(Level::debug, std::vformat(std::string(f), std::make_format_args(args...)));
        }

        template <typename... Args>
        static void info(const std::string_view f, const Args&... args){
            logImpl(Level::info, std::vformat(std::string(f), std::make_format_args(args...)));
        }

        template <typename... Args>
        static void warn(const std::string_view f, const Args&... args){
            logImpl(Level::warn, std::vformat(std::string(f), std::make_format_args(args...)));
        }

        template <typename... Args>
        static void error(const std::string_view f, const Args&... args){
            logImpl(Level::err, std::vformat(std::string(f), std::make_format_args(args...)));
        }

        template <typename... Args>
        static void fatal(const std::string_view f, const Args&... args){
            logImpl(Level::critical, std::vformat(std::string(f), std::make_format_args(args...)));
        }

    private:
        static std::shared_ptr<spdlog::logger> s_logger;

        static void logImpl(Level level, std::string msg){
            if (!s_logger) return;
            s_logger->log(level, msg);
        }
    };

    inline std::shared_ptr<spdlog::logger> Log::s_logger = nullptr;

    inline void Log::init(bool enableFile, const std::string& logPath, bool asyncMode){
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("[%H:%M:%S] [%^%l%$] %v");

        std::vector<spdlog::sink_ptr> sinks{consoleSink};
        if (enableFile) {
            try {
                auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath, true);
                fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
                sinks.push_back(fileSink);
            } catch (...) {}
        }

        if (asyncMode) {
            spdlog::init_thread_pool(8192, 1);
            s_logger = std::make_shared<spdlog::async_logger>("main", sinks.begin(), sinks.end(),
                spdlog::thread_pool(), spdlog::async_overflow_policy::block);
        } else {
            s_logger = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());
        }

        s_logger->set_level(spdlog::level::info);
        s_logger->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(s_logger);

        s_logger->info("日志系统初始化完成 (异步={}, 文件={})", asyncMode, enableFile);
    }

    inline void Log::shutdown(){
        if (s_logger) s_logger->info("日志系统关闭...");
        spdlog::shutdown();
        s_logger = nullptr;
    }

    inline void Log::setLevel(Level level){
        if (s_logger) s_logger->set_level(level);
    }

    inline void Log::flush(){
        if (s_logger) s_logger->flush();
    }
}