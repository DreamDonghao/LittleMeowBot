/// @file AdminResponse.hpp
/// @brief 管理后台 REST API 的 JSON 响应构造助手
#pragma once
#include <json/value.h>
#include <string>

namespace insoulforge::AdminResponse {
    /// @brief 成功响应 {"success":true[, "message": ...]}
    inline Json::Value okJson(const std::string &message = {}) {
        Json::Value resp;
        resp["success"] = true;
        if (!message.empty()) {
            resp["message"] = message;
        }
        return resp;
    }

    /// @brief 错误响应 {"error": ...}
    inline Json::Value errorJson(const std::string &message) {
        Json::Value resp;
        resp["error"] = message;
        return resp;
    }

    /// @brief 失败响应 {"success":false, "error": ...}
    inline Json::Value failJson(const std::string &message) {
        Json::Value resp;
        resp["success"] = false;
        resp["error"] = message;
        return resp;
    }
} // namespace insoulforge::AdminResponse
