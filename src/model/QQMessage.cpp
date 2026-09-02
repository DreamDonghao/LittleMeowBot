/// @file QQMessage.cpp
/// @brief QQ 消息模型 - 实现

#include <config/Config.hpp>
#include <fstream>
#include <model/QQMessage.hpp>
#include <service/LlmClient.hpp>
#include <spdlog/spdlog.h>
#include <util/HttpUtil.hpp>
#include <util/Logger.hpp>
#include <utility>

namespace insoulforge {
    namespace {
        drogon::Task<std::string> getImageDescribe(const std::string &imageUrl, const uint64_t groupId) {
            const auto &config = Config::instance();
            // 图像描述请求不发送 reasoning_effort（图像模型不支持，保持既有行为）
            LLMApiConfig api = config.image;
            api.reasoningEffort.clear();
            constexpr LLMModelParams params{.maxTokens = 300, .temperature = 0.7, .topP = 0.92};
            const json body = LlmClient::buildChatRequestBody(api, params,
              parseJson(fmt::format(
                R"([{{"role":"user","content":[
                    {{"type":"image_url","image_url":{{"url":"{}"}}}},
                    {{"type":"text","text":"用不到150字描述这张图片"}}
            ]}}])",
                imageUrl)));

            const auto resp = co_await HttpUtil::send("[Image]", config.image.baseUrl, config.image.path, drogon::Post,
              body, config.image.apiKey, 90.0, groupId);
            if (!resp) {
                co_return "无法识别图片";
            }

            const auto respJson = LlmClient::validChatJson(*resp);
            if (!respJson) {
                if ((*resp)->getStatusCode() != drogon::k200OK) {
                    Logger::session(groupId).error(
                      "[Image] 图像描述请求失败: status={}", static_cast<int>((*resp)->getStatusCode()));
                    co_return "无法识别图片";
                }
                Logger::session(groupId).error("[Image] 图像描述响应格式错误");
                co_return "图片识别失败";
            }

            LlmClient::logUsage(*respJson, config.image.model, "image", groupId);

            const json &message = atOrNull((*respJson)["choices"][0], "message");
            co_return jsonToString(atOrNull(message, "content"));
        }
    } // namespace

    QQMessage::QQMessage(json qqMessageJson) : m_qqMessageJson(std::move(qqMessageJson)) {
        const uint64_t qqNumber = getSenderQQNumber();
        const uint64_t selfQQ = getSelfQQNumber();
        if (m_customQQNameMap.contains(qqNumber)) {
            m_QQNameMap[qqNumber] = m_customQQNameMap[qqNumber];
        } else {
            std::string name = getSenderQQName();
            if (const std::string &botName = Config::instance().botName;
              name.find(botName) != std::string::npos && qqNumber != selfQQ) {
                name += "(昵称也为" + botName + "，但不是我)";
            }
            m_QQNameMap[qqNumber] = name;
        }
        for (const auto &item: atOrNull(m_qqMessageJson, "message")) {
            if (atOrNull(item, "type") == "at") {
                if (parseUInt64(jsonToString(atOrNull(atOrNull(item, "data"), "qq"))) == getSelfQQNumber()) {
                    m_isAtMe = true;
                }
            } else if (atOrNull(item, "type") == "reply") {
                m_replyTo = parseUInt64(jsonToString(atOrNull(atOrNull(item, "data"), "id")));
            }
        }
    }

    bool QQMessage::atMe() const { return m_isAtMe; }

    bool QQMessage::isPriorityMessage() const {
        return m_isAtMe || isPrivate() || getSenderQQNumber() == kSystemAccountId;
    }

    uint64_t QQMessage::getGroupId() const { return jsonToUInt64(atOrNull(m_qqMessageJson, "group_id")); }

    bool QQMessage::isPrivate() const { return jsonToString(atOrNull(m_qqMessageJson, "message_type")) == "private"; }

    uint64_t QQMessage::getSessionId() const {
        if (isPrivate())
            return getUserId() | kPrivateSessionFlag;
        return getGroupId();
    }

    uint64_t QQMessage::getUserId() const {
        return jsonToUInt64(atOrNull(m_qqMessageJson, "user_id"), getSenderQQNumber());
    }

    uint64_t QQMessage::getSelfQQNumber() const { return jsonToUInt64(atOrNull(m_qqMessageJson, "self_id")); }

    uint64_t QQMessage::getSenderQQNumber() const {
        return jsonToUInt64(atOrNull(atOrNull(m_qqMessageJson, "sender"), "user_id"));
    }

    std::string QQMessage::getSenderQQName() const {
        return jsonToString(atOrNull(atOrNull(m_qqMessageJson, "sender"), "nickname"));
    }

    uint64_t QQMessage::getMessageId() const { return jsonToUInt64(atOrNull(m_qqMessageJson, "message_id")); }

    drogon::Task<> QQMessage::formatMessage() {
        const uint64_t senderQQ = getSenderQQNumber();
        const uint64_t msgId = getMessageId();
        const auto senderName = std::string(getQQName(senderQQ));
        const std::string timeStr = currentDateTime();

        // 构建消息内容
        std::string textContent;
        json images = json::array();
        for (const auto &item: atOrNull(m_qqMessageJson, "message")) {
            if (atOrNull(item, "type") == "text") {
                textContent += jsonToString(atOrNull(atOrNull(item, "data"), "text"));
            } else if (atOrNull(item, "type") == "at") {
                const uint64_t atQQ = parseUInt64(jsonToString(atOrNull(atOrNull(item, "data"), "qq")));
                textContent += "@[" + std::string(getQQName(atQQ)) + ":" + std::to_string(atQQ) + "]";
            } else if (atOrNull(item, "type") == "face") {
                textContent += jsonToString(atOrNull(atOrNull(atOrNull(item, "data"), "raw"), "faceText"));
            } else if (atOrNull(item, "type") == "image") {
                textContent +=
                  "[图片：" +
                  co_await getImageDescribe(jsonToString(atOrNull(atOrNull(item, "data"), "url")), getSessionId()) +
                  "]";
                json imgInfo;
                imgInfo["file"] = getStr(atOrNull(item, "data"), "file");
                imgInfo["url"] = getStr(atOrNull(item, "data"), "url");
                images.push_back(imgInfo);
            }
            // reply类型不在这里处理，通过reply_to字段传递
        }

        // 构建JSON格式消息
        json msgJson;
        msgJson["time"] = timeStr;
        msgJson["sender"]["name"] = senderName;
        msgJson["sender"]["qq"] = std::to_string(senderQQ);
        msgJson["message_id"] = std::to_string(msgId);
        msgJson["text"] = textContent;
        if (!images.empty()) {
            msgJson["images"] = images;
        }
        if (m_replyTo > 0) {
            msgJson["reply_to"] = std::to_string(m_replyTo);
        } else {
            msgJson["reply_to"] = nullptr;
        }

        // 紧凑JSON输出（不转义Unicode）
        m_formatMessage = dumpJson(msgJson);
        co_return;
    }

    std::string QQMessage::getFormatMessage() const { return m_formatMessage; }

    std::string QQMessage::getRawMessage() const { return jsonToString(atOrNull(m_qqMessageJson, "raw_message")); }

    void QQMessage::setCustomQQName(const uint64_t qqNumber, const std::string &qqName) {
        m_customQQNameMap[qqNumber] = qqName;
        m_QQNameMap[qqNumber] = qqName;
    }

    std::string QQMessage::getQQName(const uint64_t qqNumber) {
        if (m_QQNameMap.contains(qqNumber)) {
            return m_QQNameMap[qqNumber];
        }
        return "未知";
    }

    std::unordered_map<std::string, uint64_t> QQMessage::getNameToQQMap() {
        std::unordered_map<std::string, uint64_t> nameToQQ;
        for (const auto &[qq, name]: m_QQNameMap) {
            nameToQQ[name] = qq;
        }
        return nameToQQ;
    }
} // namespace insoulforge
