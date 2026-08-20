/// @file Logger.hpp
/// @brief 日志系统生命周期 - 基于 spdlog（控制台 + 滚动文件）
/// @author donghao
/// @date 2026-08-22

#pragma once

namespace LittleMeowBot {
    /// @brief 日志系统生命周期管理
    class Logger {
    public:
        /// @brief 初始化默认 logger，应在 main() 最开始调用
        /// @details 配置控制台（彩色）与滚动文件（10MB x 5）双 sink；
        ///          文件日志不可用（如目录创建失败）时自动降级为仅控制台
        static void init();

        /// @brief 刷新并关闭日志系统，应在程序退出前调用
        static void shutdown();
    };
} // namespace LittleMeowBot