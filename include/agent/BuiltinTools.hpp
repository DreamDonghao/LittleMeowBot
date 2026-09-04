/// @file BuiltinTools.hpp
/// @brief 内置 Agent 工具注册 - 按 回复/内容获取/动作执行 三类分文件实现
/// @details 声明三类注册函数与读取工具参数的辅助函数；实现位于
///          AgentReplyTools.cpp / AgentInfoTools.cpp / AgentActionTools.cpp，
///          各工具的参数 schema 以 JSON 字面量内联在注册函数中

#pragma once

#include <string>

#include <util/JsonUtil.hpp>

namespace insoulforge {
    /// @brief 注册回复工具（REPLY，调用即结束回合）
    void registerReplyTools();

    /// @brief 注册内容获取工具（INFORMATION，查询数据、获取答案，不产生副作用）
    void registerInfoTools();

    /// @brief 注册动作执行工具（ACTION，执行操作、产生副作用）
    void registerActionTools();

    /// @brief 读取字符串参数
    /// @param args 工具调用参数
    /// @param key 参数名
    /// @return 参数值，缺失时为空串
    inline std::string argString(const json &args, const char *key) { return getStr(args, key); }
} // namespace insoulforge
