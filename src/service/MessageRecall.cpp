/// @file MessageRecall.cpp
/// @brief 消息级长期记忆召回缓存 - 实现
/// @author donghao
/// @date 2026-09-01

#include <service/MessageRecall.hpp>

#include <algorithm>
#include <config/Config.hpp>
#include <deque>
#include <drogon/utils/coroutine.h>
#include <json/json.h>
#include <mutex>
#include <service/ChatRecordManager.hpp>
#include <service/LlmClient.hpp>
#include <storage/LongTermMemoryStore.hpp>
#include <unordered_map>
#include <util/CommonUtil.hpp>
#include <util/Logger.hpp>

namespace insoulforge {
    namespace {
        constexpr int kRecallTopK = 3; // 每条消息召回的记忆条数上限
        constexpr size_t kMinRecallChars = 3; // 文本不超过该字符数（≤3 字）不召回

        // 单会话缓存：hits 为 message_id → 命中列表；order 记录入库顺序，滑出最近 kRecentRecordCount 条后淘汰
        struct SessionRecallCache {
            std::unordered_map<uint64_t, std::vector<MessageRecallHit>> hits;
            std::deque<uint64_t> order;
        };

        std::mutex g_mutex;
        std::unordered_map<uint64_t, SessionRecallCache> g_caches;

        size_t utf8Length(const std::string &text) {
            size_t count = 0;
            for (const char c: text) {
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
                    ++count;
            }
            return count;
        }

        /// @brief 写键；召回完成前已滑出窗口（order 中已无此 id）则丢弃
        void storeHits(const uint64_t sessionId, const uint64_t messageId, std::vector<MessageRecallHit> hits) {
            std::lock_guard lock(g_mutex);
            const auto it = g_caches.find(sessionId);
            if (it == g_caches.end())
                return;
            auto &cache = it->second;
            if (std::find(cache.order.begin(), cache.order.end(), messageId) == cache.order.end())
                return;
            cache.hits[messageId] = std::move(hits);
        }

        /// @brief 后台召回：向量化 → topK 相似检索 → 按注入阈值过滤 → 写键（向量化失败不留键）
        drogon::Task<> recallForMessage(const uint64_t sessionId, const uint64_t messageId, std::string text) {
            const auto embedding = co_await LlmClient::requestEmbedding(text, sessionId);
            if (!embedding) {
                Logger::session(sessionId).warn("消息召回: 向量化失败，本条消息不留召回缓存");
                co_return;
            }

            const float threshold = static_cast<float>(Config::instance().longTermInjectThreshold);
            std::vector<MessageRecallHit> hits;
            for (auto &hit: LongTermMemoryStore::searchSimilar(sessionId, *embedding, kRecallTopK)) {
                if (hit.similarity >= threshold)
                    hits.push_back({hit.id, hit.content, hit.similarity});
            }

            Logger::session(sessionId).debug("消息召回: message_id={} 命中 {} 条", messageId, hits.size());
            storeHits(sessionId, messageId, std::move(hits));
        }
    } // namespace

    void MessageRecall::onRecordAdded(
      const uint64_t sessionId, const std::string &contentJson, const bool isAssistant) {
        Json::Value content;
        if (!tryParseJson(contentJson, content) || !content.isObject())
            return;
        const uint64_t messageId = parseUInt64(content.get("message_id", "").asString());
        if (messageId == 0)
            return;

        std::string text;
        if (!isAssistant)
            text = content.get("text", "").asString();
        const bool needRecall = !isAssistant && utf8Length(text) > kMinRecallChars;

        {
            std::lock_guard lock(g_mutex);
            auto &cache = g_caches[sessionId];
            if (std::find(cache.order.begin(), cache.order.end(), messageId) != cache.order.end())
                return; // 同一条消息重复入库
            // 键入库即占位，与提示词最近 kRecentRecordCount 条记录对齐淘汰；空结果也占键防重复召回
            cache.order.push_back(messageId);
            while (cache.order.size() > kRecentRecordCount) {
                cache.hits.erase(cache.order.front());
                cache.order.pop_front();
            }
            if (!needRecall)
                cache.hits[messageId] = {};
        }

        if (needRecall)
            // async_run 接受"返回 Task 的可调用对象"，用 lambda 转发以在当前事件循环上启动后台召回
            drogon::async_run([sessionId, messageId, text = std::move(text)]() -> drogon::Task<> {
                return recallForMessage(sessionId, messageId, std::move(text));
            });
    }

    std::vector<MessageRecallHit> MessageRecall::getHits(const uint64_t sessionId, const uint64_t messageId) {
        std::lock_guard lock(g_mutex);
        const auto it = g_caches.find(sessionId);
        if (it == g_caches.end())
            return {};
        const auto hitIt = it->second.hits.find(messageId);
        return hitIt != it->second.hits.end() ? hitIt->second : std::vector<MessageRecallHit>{};
    }
} // namespace insoulforge
