/// @file CommonUtil.hpp
/// @brief 通用工具函数
/// @author donghao
/// @date 2026-04-02
/// @details 提供通用工具函数：
///          - JSON 解析：parseJson() / tryParseJson()
///          - JSON 序列化：dumpJson()
///          - LLM 输出净化：tryExtractJsonObject()
///          - 时间获取与格式化：currentDateTime() / formatUnixTime() / formatTimeOfDay()
///          - 无符号整数解析：parseUInt64()
///          - 文本修剪：trim()

#pragma once
#include <cctype>
#include <charconv>
#include <ctime>
#include <drogon/drogon.h>
#include <json/writer.h>
#include <optional>
#include <spdlog/spdlog.h>
#include <string_view>

[[nodiscard]] inline Json::Value parseJson(const std::string &jsonStr) {
    Json::CharReaderBuilder builder;
    builder["collectComments"] = false;

    std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    Json::Value root;
    std::string errs;

    const char *begin = jsonStr.data();

    if (const char *end = begin + jsonStr.size(); !reader->parse(begin, end, &root, &errs)) {
        // 出错时打印并返回空对象
        spdlog::warn("JSON解析失败: {}", errs);
        return Json::Value{};
    }
    return root;
}

/// @brief 尝试解析 JSON（非抛出、不打印日志）
/// @param jsonStr 输入字符串
/// @param root 输出的 JSON 值；解析失败时被置为 null
/// @return 是否解析成功
[[nodiscard]] inline bool tryParseJson(const std::string_view jsonStr, Json::Value &root) {
    root = Json::Value{};
    const Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(jsonStr.data(), jsonStr.data() + jsonStr.size(), &root, &errs);
}

/// @brief 序列化为紧凑 JSON（indentation=""，与聊天记录等既有存储格式一致）
/// @param emitUtf8 是否原样输出非 ASCII 字符（false 时转义为 \\uXXXX）
[[nodiscard]] inline std::string dumpJson(const Json::Value &value, const bool emitUtf8 = true) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "";
    builder["emitUTF8"] = emitUtf8;
    return Json::writeString(builder, value);
}

/// @brief 容忍模型输出 ```json 围栏等杂质：截取首尾大括号之间的内容
[[nodiscard]] inline bool tryExtractJsonObject(const std::string &text, std::string &payload) {
    const size_t start = text.find('{');
    const size_t end = text.rfind('}');
    if (start == std::string::npos || end == std::string::npos || end <= start)
        return false;
    payload = text.substr(start, end - start + 1);
    return true;
}

/// @brief 去除字符串首尾空白（isspace 语义）
[[nodiscard]] inline std::string trim(std::string s) {
    const auto isSpace = [](const unsigned char c) { return std::isspace(c) != 0; };
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

/// @brief 尝试解析无符号整数（非抛出，替代 std::stoull）
/// @param s 输入字符串
/// @return 解析结果；要求整串都是数字（允许前导空白），否则返回 nullopt
[[nodiscard]] inline std::optional<uint64_t> tryParseUInt64(std::string_view s) {
    uint64_t value = 0;
    const auto *begin = s.data();
    const auto *end = s.data() + s.size();
    // 跳过前导空白（与 stoull 行为一致）
    while (begin < end && (*begin == ' ' || *begin == '\t'))
        ++begin;
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end)
        return std::nullopt;
    return value;
}

/// @brief 解析无符号整数（非抛出，替代 std::stoull）
/// @param s 输入字符串
/// @param fallback 解析失败时返回的值
/// @return 解析结果；要求整串都是数字，否则返回 fallback
[[nodiscard]] inline uint64_t parseUInt64(std::string_view s, uint64_t fallback = 0) {
    return tryParseUInt64(s).value_or(fallback);
}

/// @brief 将 Json::Value 安全转换为 uint64_t（非抛出，兼容数字与字符串类型）
/// @param v JSON 值（可能来自外部 OneBot 协议，字段类型不稳定）
/// @param fallback 无法转换时返回的值
/// @return 数值；字符串按十进制解析，缺失/空/null/负数/浮点一律返回 fallback
[[nodiscard]] inline uint64_t jsonToUInt64(const Json::Value &v, uint64_t fallback = 0) {
    if (v.isString())
        return parseUInt64(v.asString(), fallback);
    if (!v.isNull() && v.isConvertibleTo(Json::uintValue))
        return v.asUInt64();
    return fallback;
}

#include <chrono>
#include <fmt/chrono.h>

/// @brief time_t 转本地 std::tm（各平台的安全转换）
inline std::tm localTime(const std::time_t t) {
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

inline std::string currentDateTime() {
    using namespace std::chrono;
    return fmt::format("{:%Y-%m-%d %H:%M:%S}", localTime(system_clock::to_time_t(system_clock::now())));
}

/// @brief unix 秒格式化为本地时间 YYYY-MM-DD HH:MM:SS
[[nodiscard]] inline std::string formatUnixTime(const int64_t unixSec) {
    return fmt::format("{:%Y-%m-%d %H:%M:%S}", localTime(static_cast<std::time_t>(unixSec)));
}

/// @brief unix 秒格式化为本地时间 HH:MM
[[nodiscard]] inline std::string formatTimeOfDay(const int64_t unixSec) {
    return fmt::format("{:%H:%M}", localTime(static_cast<std::time_t>(unixSec)));
}