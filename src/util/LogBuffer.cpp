/// @file LogBuffer.cpp
/// @brief 运行日志内存缓冲区与查询服务 - 实现

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fstream>
#include <regex>
#include <service/LogWebSocketManager.hpp>
#include <spdlog/details/log_msg.h>
#include <util/LogBuffer.hpp>

namespace insoulforge {
    namespace {
        constexpr size_t kMaxEntries = 5000;
        // 与 QQMessage::kPrivateSessionFlag 保持一致（util 层不反向依赖 model）
        constexpr uint64_t kPrivateSessionFlag = 1ULL << 63;
        const std::regex kSessionPatterns[] = {
          std::regex(R"((?:群|sessionId|group_id)[ =:：]+([0-9]{1,20}))", std::regex::icase),
          std::regex(R"(群([0-9]{1,20}))")};
        // 私聊日志前缀（Logger 对私聊会话输出 [private_id=QQ]），QQ 号需还原为带标志位的会话 ID
        const std::regex kPrivatePattern(R"(private_id[ =:：]+([0-9]{1,11}))");
    } // namespace

    LogBuffer &LogBuffer::instance() {
        static LogBuffer buffer;
        return buffer;
    }

    void LogBuffer::loadFromDirectory(const std::string &directory) {
        std::vector<LogEntry> loaded;
        for (int index = 5; index >= 0; --index) {
            const auto path =
              std::filesystem::path(directory) / (index == 0 ? "bot.log" : fmt::format("bot.log.{}", index));
            std::ifstream file(path);
            if (!file) {
                continue;
            }

            std::string line;
            while (std::getline(file, line)) {
                if (auto entry = parseLine(line)) {
                    loaded.push_back(std::move(*entry));
                }
            }
        }

        std::lock_guard lock(m_mutex);
        m_entries.clear();
        const auto first = loaded.size() > kMaxEntries ? loaded.size() - kMaxEntries : 0;
        for (size_t index = first; index < loaded.size(); ++index) {
            loaded[index].id = m_nextId++;
            m_entries.push_back(std::move(loaded[index]));
        }
    }

    void LogBuffer::append(const spdlog::details::log_msg &message) {
        LogEntry entry;
        entry.timestamp = formatTimestamp(message.time);
        const auto level = spdlog::level::to_string_view(message.level);
        entry.level = std::string(level.data(), level.size());
        if (entry.level == "warning")
            entry.level = "warn";
        if (entry.level == "err")
            entry.level = "error";
        entry.message = std::string(message.payload.data(), message.payload.size());
        entry.sessionId = extractSessionId(entry.message);

        Json::Value evt;
        {
            std::lock_guard lock(m_mutex);
            entry.id = m_nextId++;
            if (m_entries.size() >= kMaxEntries) {
                m_entries.erase(m_entries.begin());
            }
            evt["id"] = entry.id;
            evt["timestamp"] = entry.timestamp;
            evt["level"] = entry.level;
            evt["message"] = entry.message;
            // 会话 ID 可能带私聊标志位（超过 JS 安全整数范围），序列化为字符串
            evt["groupId"] = entry.sessionId.has_value() ? Json::Value(std::to_string(*entry.sessionId))
                                                         : Json::Value(Json::nullValue);
            m_entries.push_back(entry);
        }
        LogWebSocketManager::instance().pushLog(evt);
    }

    LogQueryResult LogBuffer::query(const LogQuery &query) const {
        std::lock_guard lock(m_mutex);
        LogQueryResult result;
        if (!m_entries.empty()) {
            result.oldestId = m_entries.front().id;
            result.newestId = m_entries.back().id;
        }

        std::vector<const LogEntry *> matched;
        matched.reserve(m_entries.size());
        for (const auto &entry: m_entries) {
            if (matches(entry, query)) {
                matched.push_back(&entry);
            }
        }
        if (matched.empty()) {
            return result;
        }

        size_t start = 0;
        size_t end = matched.size();
        if (query.afterId > 0) {
            start = std::distance(matched.begin(),
              std::ranges::find_if(matched, [&](const auto *entry) { return entry->id > query.afterId; }));
            end = std::min(start + query.limit, matched.size());
        } else if (query.beforeId.has_value()) {
            end = std::distance(matched.begin(),
              std::ranges::find_if(matched, [&](const auto *entry) { return entry->id >= *query.beforeId; }));
            if (end > query.limit) {
                start = end - query.limit;
            }
        } else {
            if (end > query.limit) {
                start = end - query.limit;
            }
        }

        result.entries.reserve(end - start);
        for (size_t index = start; index < end; ++index) {
            result.entries.push_back(*matched[index]);
        }
        result.hasMore = start > 0 || end < matched.size();
        if (!result.entries.empty()) {
            result.nextBeforeId = result.entries.front().id;
            result.nextAfterId = result.entries.back().id;
        }
        return result;
    }

    size_t LogBuffer::size() const {
        std::lock_guard lock(m_mutex);
        return m_entries.size();
    }

    std::optional<LogEntry> LogBuffer::parseLine(const std::string &line) {
        static const std::regex pattern(
          R"(^\[([0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3})\] \[([a-z]+)\] (.*)$)",
          std::regex::icase);
        std::smatch match;
        if (!std::regex_match(line, match, pattern)) {
            return std::nullopt;
        }

        LogEntry entry;
        entry.timestamp = match[1].str();
        entry.level = match[2].str();
        entry.message = match[3].str();
        entry.sessionId = extractSessionId(entry.message);
        return entry;
    }

    std::optional<uint64_t> LogBuffer::extractSessionId(const std::string &message) {
        std::smatch match;
        if (std::regex_search(message, match, kPrivatePattern)) {
            try {
                return std::stoull(match[1].str()) | kPrivateSessionFlag;
            } catch (const std::exception &) {
                return std::nullopt;
            }
        }
        for (const auto &pattern: kSessionPatterns) {
            if (std::regex_search(message, match, pattern)) {
                try {
                    return std::stoull(match[1].str());
                } catch (const std::exception &) {
                    return std::nullopt;
                }
            }
        }
        return std::nullopt;
    }

    std::string LogBuffer::formatTimestamp(const spdlog::log_clock::time_point &timestamp) {
        const auto time = spdlog::log_clock::to_time_t(timestamp);
        std::tm localTime{};
#ifdef _WIN32
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif
        const auto milliseconds =
          std::chrono::duration_cast<std::chrono::milliseconds>(timestamp.time_since_epoch()).count() % 1000;
        return fmt::format("{:%Y-%m-%d %H:%M:%S}.{:03d}", localTime, milliseconds);
    }

    bool LogBuffer::matches(const LogEntry &entry, const LogQuery &query) {
        if (query.systemOnly && entry.sessionId.has_value()) {
            return false;
        }
        if (query.sessionId.has_value() && entry.sessionId != query.sessionId) {
            return false;
        }
        if (query.level.has_value() && entry.level != *query.level) {
            return false;
        }
        return query.keyword.empty() || entry.message.find(query.keyword) != std::string::npos;
    }
} // namespace insoulforge
