/// @file LongTermMemory.cpp
/// @brief 长期记忆服务 - 实现
/// @author donghao
/// @date 2026-09-01

#include <service/LongTermMemory.hpp>

#include <service/LlmClient.hpp>
#include <storage/LongTermMemoryStore.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    drogon::Task<bool> LongTermMemory::addMemory(const std::string &content, const uint64_t sessionId) {
        const auto embedding = co_await LlmClient::requestEmbedding(content, sessionId);
        if (!embedding) {
            Logger::session(sessionId).warn("长期记忆向量化失败（Embedding 未配置或请求失败），本条不入库");
            co_return false;
        }

        if (!LongTermMemoryStore::instance().addMemory(sessionId, content, *embedding)) {
            Logger::session(sessionId).error("长期记忆写入数据库失败");
            co_return false;
        }

        Logger::session(sessionId).info("长期记忆已写入: {} 字符", content.size());
        co_return true;
    }

    drogon::Task<std::optional<std::string>> LongTermMemory::searchMemory(
      const std::string &query, const int topK, const uint64_t sessionId) {
        const auto embedding = co_await LlmClient::requestEmbedding(query, sessionId);
        if (!embedding) {
            Logger::session(sessionId).warn("记忆检索向量化失败（Embedding 未配置或请求失败）");
            co_return std::nullopt;
        }

        const auto rows = LongTermMemoryStore::instance().searchSimilar(sessionId, *embedding, topK);
        std::string result;
        for (const auto &memory: rows) {
            if (memory.similarity < 0.3f)
                continue;
            result += memory.content + "\n";
        }

        if (result.empty())
            co_return std::optional<std::string>("未找到相关信息");
        co_return result;
    }
} // namespace insoulforge
