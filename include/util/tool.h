/// @file tool.h
/// @brief 通用工具函数
/// @author donghao
/// @date 2026-04-02
/// @details 提供通用工具函数：
///          - JSON 解析：parseJson()
///          - 时间获取：currentDateTime()

#pragma once
#include <drogon/drogon.h>

inline Json::Value parseJson(const std::string& jsonStr){
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

#include <chrono>
#include <ctime>
#include <fmt/chrono.h>
#include <fmt/core.h>

inline std::string currentDateTime(){
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    return fmt::format("{:%Y-%m-%d %H:%M:%S}", tm);
}