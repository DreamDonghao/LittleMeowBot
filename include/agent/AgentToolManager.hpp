/// @file AgentToolManager.hpp
/// @brief Agent 工具管理器 - 注册所有可用工具
/// @author donghao
/// @date 2026-04-02
/// @details 负责注册和管理 Agent 可使用的工具：
///          - 终端工具：no_reply, reply
///          - 信息工具：get_weather, search_web, get_time, search_knowledge, recall_memory
///          - 动作工具：random, send_face, send_image, send_emoji, at_user, ban_user
///          - 自定义工具：从数据库加载用户定义的工具

#pragma once
#include <service/ToolRegistry.hpp>
#include <service/RAGFlowClient.hpp>
#include <api/ApiClient.hpp>
#include <storage/Database.hpp>
#include <drogon/utils/coroutine.h>
#include <drogon/HttpClient.h>
#include <json/value.h>
#include <spdlog/spdlog.h>
#include <fmt/core.h>
#include <string>
#include <unordered_map>
#include <random>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <array>

namespace LittleMeowBot {
    // 工具限制常量
    static constexpr int MAX_RANDOM_COUNT = 10;

    /// @brief 工具管理器 - 注册所有可用工具
    class AgentToolManager{
    public:
        static AgentToolManager& instance();

        /// @brief 注册所有工具
        void registerAllTools() const;

        /// @brief 注册自定义工具（从数据库加载）
        void registerCustomTools() const;

        /// @brief 执行 Python 脚本工具
        /// @param scriptContent Python脚本内容（直接存储在数据库中）
        /// @param args 传入参数
        static drogon::Task<std::string> executePythonTool(const std::string& scriptContent, const Json::Value& args);

        /// @brief 执行 HTTP 工具
        static drogon::Task<std::string> executeHttpTool(const std::string& config, const Json::Value& args);

    private:
        AgentToolManager() = default;
    };
}
