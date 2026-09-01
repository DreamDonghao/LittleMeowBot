/// @file AgentToolManager.hpp
/// @brief Agent 工具管理器 - 注册所有可用工具
/// @author donghao
/// @date 2026-04-02
/// @details 负责注册和管理 Agent 可使用的工具：
///          - 终端工具：no_reply, reply
///          - 信息工具：list_stickers, search_knowledge, recall_memory, get_group_name
///          - 动作工具：send_face, send_image, send_sticker, save_sticker, rename_sticker, delete_sticker,
///                    at_user, ban_user, send_poke, recall_message, create_scheduled_task
///          - 自定义工具：从数据库加载用户定义的工具（支持Python/HTTP）

#pragma once
#include <service/LlmClient.hpp>
#include <drogon/utils/coroutine.h>
#include <optional>
#include <spdlog/spdlog.h>
#include <string>

/// @brief 工具管理器 - 注册所有可用工具
namespace insoulforge::AgentToolManager {
    /// @brief 注册所有工具
    void registerAllTools();

    /// @brief 注册自定义工具（从数据库加载）
    void registerCustomTools();

    /// @brief 执行 Python 脚本工具
    /// @param scriptContent Python脚本内容（直接存储在数据库中）
    /// @param args 传入参数
    drogon::Task<std::string> executePythonTool(const std::string &scriptContent, const Json::Value &args);

    /// @brief 执行 HTTP 工具
    drogon::Task<std::string> executeHttpTool(const std::string &config, const Json::Value &args);

    /// @brief 获取 QQ 收藏表情列表（调用 NapCat fetch_custom_face_detail，带60秒缓存）
    /// @return 归一化后的表情数组，失败时返回空数组
    drogon::Task<Json::Value> fetchFavoriteEmojis(std::optional<uint64_t> sessionId = std::nullopt);

    /// @brief 在收藏表情列表中按名称查找表情（名称 = desc 或 "表情N"）
    drogon::Task<Json::Value> findFavoriteEmoji(
      const std::string &name, std::optional<uint64_t> sessionId = std::nullopt);

    /// @brief 使收藏表情缓存失效（修改/删除后调用）
    void invalidateFavoriteEmojiCache();
} // namespace insoulforge::AgentToolManager
