/// @file Logger.hpp
/// @brief 日志系统生命周期 - 基于 spdlog（控制台 + 滚动文件）
/// @author donghao
/// @date 2026-08-22

#pragma once

#include <cstdint>
#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <string_view>
#include <utility>

namespace LittleMeowBot {
    class GroupLogger {
    public:
        explicit GroupLogger(uint64_t groupId) : m_groupId(groupId) {
        }

        template<typename... Args>
        void trace(fmt::format_string<Args...> format, Args&&... args) const {
            write(spdlog::level::trace, fmt::format(format, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void debug(fmt::format_string<Args...> format, Args&&... args) const {
            write(spdlog::level::debug, fmt::format(format, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void info(fmt::format_string<Args...> format, Args&&... args) const {
            write(spdlog::level::info, fmt::format(format, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void warn(fmt::format_string<Args...> format, Args&&... args) const {
            write(spdlog::level::warn, fmt::format(format, std::forward<Args>(args)...));
        }

        template<typename... Args>
        void error(fmt::format_string<Args...> format, Args&&... args) const {
            write(spdlog::level::err, fmt::format(format, std::forward<Args>(args)...));
        }

    private:
        void write(spdlog::level::level_enum level, const std::string& message) const {
            spdlog::log(level, "[group_id={}] {}", m_groupId, message);
        }

        uint64_t m_groupId;
    };

    /// @brief 日志系统生命周期管理
    class Logger {
    public:
        /// @brief 初始化默认 logger，应在 main() 最开始调用
        /// @details 配置控制台（彩色）与滚动文件（10MB x 5）双 sink；
        ///          文件日志不可用（如目录创建失败）时自动降级为仅控制台
        static void init();

        /// @brief 设置运行时日志等级
        /// @return 等级名称有效时返回 true
        static bool setLevel(std::string_view levelName);

        /// @brief 获取当前运行时日志等级
        static std::string level();

        /// @brief 创建带群聊上下文的日志记录器
        static GroupLogger group(uint64_t groupId);

        /// @brief 刷新并关闭日志系统，应在程序退出前调用
        static void shutdown();
    };
} // namespace LittleMeowBot