/// @file QQMessage.cpp
/// @brief QQ 消息模型 - 实现

#include <model/QQMessage.hpp>
#include <util/tool.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <config/Config.hpp>

namespace LittleMeowBot {
    drogon::Task<std::string> getImageDescribe(const std::string& imageUrl){
        const auto& config = Config::instance();
        const auto client = drogon::HttpClient::newHttpClient(config.image.baseUrl);
        Json::Value body;
        body["model"] = config.image.model;
        body["messages"] = parseJson(fmt::format(
            R"([{{"role":"user","content":[
                    {{"type":"image_url","image_url":{{"url":"{}"}}}},
                    {{"type":"text","text":"用不到150字描述这张图片"}}
    ]}}])",
            imageUrl));
        body["temperature"] = 0.7;
        body["max_tokens"] = 300;
        body["top_p"] = 0.92;

        const auto req = drogon::HttpRequest::newHttpJsonRequest(body);
        req->setMethod(drogon::Post);
        req->setPath(config.image.path);
        req->addHeader("Authorization", "Bearer " + config.image.apiKey);

        const auto resp = co_await client->sendRequestCoro(req);
        if (resp->getStatusCode() != drogon::k200OK) {
            spdlog::error("图像描述请求失败: status={}", resp->getStatusCode());
            co_return "无法识别图片";
        }
        const auto json = resp->getJsonObject();
        if (!json || !json->isMember("choices")) {
            spdlog::error("图像描述响应格式错误");
            co_return "图片识别失败";
        }
        const auto& choices = (*json)["choices"];
        const auto& content = choices[0]["message"]["content"].asString();

        co_return content;
    }

    QQMessage::QQMessage(const Json::Value& qqMessageJson){
        setMessageJson(qqMessageJson);
    }

    void QQMessage::setMessageJson(const Json::Value& qqMessageJson){
        m_qqMessageJson = &qqMessageJson;
        const Json::UInt64 qqNumber = getSenderQQNumber();
        const Json::UInt64 selfQQ = getSelfQQNumber();
        if (m_customQQNameMap.contains(qqNumber)) {
            m_QQNameMap[qqNumber] = m_customQQNameMap[qqNumber];
        } else {
            std::string name = getSenderQQName();
            if (const std::string& botName = Config::instance().botName;
                name.find(botName) != std::string::npos && qqNumber != selfQQ) {
                name += "(昵称也为" + botName + "，但不是我)";
            }
            m_QQNameMap[qqNumber] = name;
        }
        for (const auto& item : (*m_qqMessageJson)["message"]) {
            if (item["type"] == "at") {
                if (std::stoul(item["data"]["qq"].asString()) == getSelfQQNumber()) {
                    m_isAtMe = true;
                }
            } else if (item["type"] == "image") {
                m_isExistImage = true;
            } else if (item["type"] == "reply") {
                m_replyTo = std::stoull(item["data"]["id"].asString());
            }
        }
    }

    bool QQMessage::atMe() const{ return m_isAtMe; }
    bool QQMessage::existImage() const{ return m_isExistImage; }
    Json::UInt64 QQMessage::getGroupId() const{ return (*m_qqMessageJson)["group_id"].asUInt64(); }
    Json::UInt64 QQMessage::getSelfQQNumber() const{ return (*m_qqMessageJson)["self_id"].asUInt64(); }
    Json::UInt64 QQMessage::getSenderQQNumber() const{ return (*m_qqMessageJson)["sender"]["user_id"].asUInt64(); }
    Json::String QQMessage::getSenderQQName() const{ return (*m_qqMessageJson)["sender"]["nickname"].asString(); }
    Json::String QQMessage::getSenderGroupName() const{ return (*m_qqMessageJson)["sender"]["card"].asString(); }
    Json::UInt64 QQMessage::getMessageId() const{ return (*m_qqMessageJson)["message_id"].asUInt64(); }
    uint64_t QQMessage::getReplyTo() const{ return m_replyTo; }

    drogon::Task<> QQMessage::formatMessage(){
        const uint64_t senderQQ = getSenderQQNumber();
        const uint64_t msgId = getMessageId();
        const std::string senderName = std::string(getQQName(senderQQ));
        const std::string timeStr = currentDateTime();

        // 构建消息内容
        std::string textContent;
        for (const auto& item : (*m_qqMessageJson)["message"]) {
            if (item["type"] == "text") {
                textContent += item["data"]["text"].asString();
            } else if (item["type"] == "at") {
                const uint64_t atQQ = std::stoull(item["data"]["qq"].asString());
                textContent += "@[" + std::string(getQQName(atQQ)) + ":" + std::to_string(atQQ) + "]";
            } else if (item["type"] == "face") {
                textContent += item["data"]["raw"]["faceText"].asString();
            } else if (item["type"] == "image") {
                textContent += "[图片：" + co_await getImageDescribe(item["data"]["url"].asString()) + "]";
            }
            // reply类型不在这里处理，通过reply_to字段传递
        }

        // 构建JSON格式消息
        Json::Value msgJson;
        msgJson["time"] = timeStr;
        msgJson["sender"]["name"] = senderName;
        msgJson["sender"]["qq"] = std::to_string(senderQQ);
        msgJson["message_id"] = std::to_string(msgId);
        msgJson["text"] = textContent;
        if (m_replyTo > 0) {
            msgJson["reply_to"] = std::to_string(m_replyTo);
        } else {
            msgJson["reply_to"] = Json::nullValue;
        }

        // 紧凑JSON输出（不转义Unicode）
        Json::StreamWriterBuilder writerBuilder;
        writerBuilder["indentation"] = "";
        writerBuilder["emitUTF8"] = true;
        m_formatMessage = Json::writeString(writerBuilder, msgJson);

        // 缓存消息
        m_messageCache[msgId] = m_formatMessage;
        Database::instance().cacheMessage(msgId, m_formatMessage);
        co_return;
    }

    Json::String QQMessage::getFormatMessage() const{ return m_formatMessage; }
    std::string QQMessage::getRawMessage() const{ return (*m_qqMessageJson)["raw_message"].asString(); }

    void QQMessage::setCustomQQName(const Json::UInt64 qqNumber, const Json::String& qqName){
        m_customQQNameMap[qqNumber] = qqName;
        m_QQNameMap[qqNumber] = qqName;
    }

    Json::String QQMessage::getQQName(const Json::UInt64 qqNumber){
        if (m_QQNameMap.contains(qqNumber)) {
            return m_QQNameMap[qqNumber];
        }
        return "未知";
    }

    std::unordered_map<std::string, uint64_t> QQMessage::getNameToQQMap(){
        std::unordered_map<std::string, uint64_t> nameToQQ;
        for (const auto& [qq, name] : m_QQNameMap) {
            nameToQQ[name] = qq;
        }
        return nameToQQ;
    }

    void QQMessage::addMessageCache(const Json::UInt64 messageId, Json::String message){
        if (constexpr size_t MAX_CACHE_SIZE = 100;
            m_messageCache.size() >= MAX_CACHE_SIZE) {
            size_t toRemove = MAX_CACHE_SIZE / 2;
            auto it = m_messageCache.begin();
            while (toRemove-- > 0 && it != m_messageCache.end()) {
                it = m_messageCache.erase(it);
            }
        }
        m_messageCache[messageId] = std::move(message);
        Database::instance().cacheMessage(messageId, m_messageCache[messageId]);
    }
}