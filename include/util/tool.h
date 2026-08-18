/// @file tool.h
/// @brief 通用工具函数
/// @author donghao
/// @date 2026-04-02
/// @details 提供通用工具函数：
///          - JSON 解析：parseJson()
///          - 时间获取：currentDateTime()
///          - 无符号整数解析：parseUInt64()

#pragma once
#include <drogon/drogon.h>
#include <charconv>
#include <optional>
#include <string_view>

[[nodiscard]] inline Json::Value parseJson(const std::string& jsonStr){
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string errs;

    const char* begin = jsonStr.data();

    if (const char* end = begin + jsonStr.size(); !reader->parse(begin, end, &root, &errs)) {
        // 出错时打印并返回空对象
        std::cerr << "JSON解析失败: " << errs << "\n";
        return Json::Value{};
    }
    return root;
}

/// @brief 尝试解析无符号整数（非抛出，替代 std::stoull）
/// @param s 输入字符串
/// @return 解析结果；要求整串都是数字（允许前导空白），否则返回 nullopt
[[nodiscard]] inline std::optional<uint64_t> tryParseUInt64(std::string_view s){
    uint64_t value = 0;
    const auto* begin = s.data();
    const auto* end = s.data() + s.size();
    // 跳过前导空白（与 stoull 行为一致）
    while (begin < end && (*begin == ' ' || *begin == '\t')) ++begin;
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) return std::nullopt;
    return value;
}

/// @brief 解析无符号整数（非抛出，替代 std::stoull）
/// @param s 输入字符串
/// @param fallback 解析失败时返回的值
/// @return 解析结果；要求整串都是数字，否则返回 fallback
[[nodiscard]] inline uint64_t parseUInt64(std::string_view s, uint64_t fallback = 0){
    return tryParseUInt64(s).value_or(fallback);
}

#include <chrono>
#include <fmt/chrono.h>

inline std::string currentDateTime() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};  // 确保初始化为零

#ifdef _WIN32
    localtime_s(&tm, &t);   // Windows 安全版本，参数顺序 (tm*, const time_t*)
#else
    localtime_r(&t, &tm);   // POSIX 标准版本，参数顺序 (const time_t*, tm*)
#endif

    return fmt::format("{:%Y-%m-%d %H:%M:%S}", tm);
}