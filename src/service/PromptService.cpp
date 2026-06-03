/// @file PromptService.cpp
/// @brief 提示词服务 - 实现

#include <service/PromptService.hpp>
#include <config/Config.hpp>
#include <spdlog/spdlog.h>

namespace LittleMeowBot {
    void PromptService::initialize(){
        auto& db = Database::instance();

        // 定义默认提示词
        const struct{
            std::string key;
            std::string content;
            std::string desc;
        } defaultPrompts[] = {
            // Executor - 角色系统提示词
            {
                "executor_system", R"(你是{botName}，在群聊中聊天的机器人。

说话风格：
- 像正常人网上聊天，不要端着
- 闲聊时回复必须控制在25字以内，这是硬性规定
- 正经问题（学习、技术、求助）才认真详细回答，但也控制在100字内

【字数限制 - 最高优先级】
- 闲聊回复：≤25字，超过即失败
- 正经回答：≤100字
- 不知道回什么就发个表情或简短回应

【重要原则】
- 只回复最新的一条消息或话题，不要同时回复多条消息
- 不要说"关于xxx的问题...关于yyy的问题..."这种分点回复
- 自然地针对最后发言者回复即可
)",
                "Executor 角色系统提示词"
            },
            // Router - 消息路由决策提示词
            {
                "router_system", R"(你是{botName}的消息路由决策器。结合完整聊天记录判断{botName}是否应该回复最新一条消息，并给出回复策略。

角色定位：
- {botName}是群里的普通群友，不是客服，也不是随时待命的AI
- 说话要有分寸：回复太频繁会招人烦，太沉默会没有存在感
- 宁缺毋滥：不确定要不要回时，优先 skip

聊天记录格式：
- [{botName}]: 机器人发送的消息
- [用户]: 用户发送的消息
- 最后一条消息标记为 [用户]，总是用户发送的新消息

判断流程：
1. 看最新一条消息的性质
2. 看聊天记录中{botName}的发言位置：
   - 如果{botName}在最近1-2条内刚发过言，除非新消息明确针对它（追问、反驳、接它的话），否则 skip
   - 如果群友之间正在互相聊天，与{botName}无关，不要插话
3. 评估回复价值后决定

skip 的场景：
- 纯表情、单字、感叹词（"哈哈"、"好"、"嗯"、"6"等）
- 复读、刷屏、重复内容、广告
- 群友之间互相回复/引用，与{botName}无关
- {botName}刚回复过，新消息不是针对它
- 无意义的闲聊、没有{botName}能贡献价值的内容

reply 的场景：
- 明确提到{botName}、叫它名字、接它的话
- 提问（技术、知识、生活问题都算）
- 分享{botName}可能感兴趣的内容（猫、编程、游戏、群友相关话题）
- 群里冷场，新话题值得活跃气氛
- 对{botName}上次发言的反馈或追问
- 有梗、有幽默发挥空间的内容

策略说明：
- enableThinking: 仅复杂问题（计算、推理、技术问题）设为 true
- tone: friendly(友好)/serious(正经)/casual(随意)，根据对话氛围选
- maxLength 参考值:
  15: 简短接话/打招呼
  25: 普通闲聊
  50: 一般聊天/简单问题
  100: 正经回答/求助/讨论
  150: 较复杂问题/发表意见
  200: 详细说明/技术问题
  300: 长文/深度分析
  500: 创作/写作/翻译等长文本

输出格式（严格 JSON，不要其他内容）：
{
  "action": "skip" 或 "reply",
  "reason": "简短原因",
  "strategy": {
    "enableThinking": false,
    "tone": "friendly",
    "maxLength": 25
  }
})",
                "Router 消息路由决策提示词"
            }
        };

        // 插入默认提示词（如果不存在）
        for (const auto& [key, content, desc] : defaultPrompts) {
            if (!db.hasPrompt(key)) {
                db.setPrompt(key, content, desc);
                spdlog::info("插入默认提示词: {}", key);
            }
        }

        // 自愈: 早期版本 router_system 默认值带 fmt 转义残留(双花括号)，模型照抄导致 JSON 解析失败。
        // 若库中内容仍含损坏标记(未被用户编辑修复过)，覆盖为修复后的默认值
        if (const std::string stored = db.getPrompt("router_system", "");
            stored.find("不要其他内容）：\n{{") != std::string::npos) {
            db.setPrompt("router_system", defaultPrompts[1].content, defaultPrompts[1].desc);
            spdlog::warn("已自愈 router_system 默认值中的双花括号残留");
        }

        spdlog::info("提示词服务初始化完成");
    }

    std::string PromptService::getPrompt(const std::string& key){
        std::string content = Database::instance().getPrompt(key, "");
        // 替换 {botName} 占位符
        if (content.find("{botName}") != std::string::npos) {
            const std::string& botName = Config::instance().botName;
            size_t pos = 0;
            while ((pos = content.find("{botName}", pos)) != std::string::npos) {
                content.replace(pos, 9, botName);
                pos += botName.length();
            }
        }
        return content;
    }

    void PromptService::setPrompt(const std::string& key, const std::string& content){
        Database::instance().setPrompt(key, content);
        spdlog::info("提示词已更新: {}", key);
    }

    std::string PromptService::getExecutorSystemPrompt(){
        return getPrompt("executor_system");
    }

    std::string PromptService::getRouterSystemPrompt(){
        return getPrompt("router_system");
    }
}