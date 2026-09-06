# insoulforge 开发文档

面向开发者的构建、架构与贡献指南。使用说明见 [README](../README.md)，编码规范见 [CODING_STYLE.md](./CODING_STYLE.md)。

## 环境要求

| 依赖                             | 版本要求            | 说明                                                 |
|----------------------------------|---------------------|------------------------------------------------------|
| CMake                            | ≥ 3.20              | 构建系统                                             |
| GCC / Clang                      | GCC 13+ / Clang 14+ | 代码使用 `<format>`，GCC 11/12 不支持                |
| Node.js + npm                    | 任意较新版本        | 必需：CMake 配置阶段查找 npm，后端构建会连带构建前端 |
| Drogon                           | 1.8+ 推荐           | 异步 Web 框架                                        |
| spdlog / fmt / jsoncpp / SQLite3 | 任意较新版本        | 日志、格式化与存储                                   |

**Ubuntu 24.04（推荐，apt 开箱即用）**：

```bash
sudo apt update && sudo apt install -y \
    cmake g++ \
    libdrogon-dev \
    libspdlog-dev \
    libfmt-dev \
    libsqlite3-dev \
    libjsoncpp-dev \
    libssl-dev \
    libbrotli-dev \
    zlib1g-dev \
    libuuid1
```

**Ubuntu 22.04** 默认 g++ 11 不支持 `<format>`，需额外安装 GCC 13：

```bash
sudo add-apt-repository ppa:ubuntu-toolchain-r/test
sudo apt update && sudo apt install -y g++-13
# 构建时指定编译器
cmake -DCMAKE_CXX_COMPILER=g++-13 ..
```

**macOS（Homebrew）**：

```bash
brew install cmake drogon spdlog fmt jsoncpp sqlite3 openssl brotli node
```

用 CLion 打开项目直接构建（`cmake-build-debug/`），无需额外配置。

## 构建

### 一次性构建（发布）

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j    # Linux 可用 -j$(nproc)
```

构建产物：

- 可执行文件 → `build/insoulforge/exe/insoulforge`
- 前端静态文件 → `build/insoulforge/public/`（CMake 会在前端源码变化时自动执行 `npm run build`）

### 日常开发

推荐使用 CLion（`cmake-build-debug/` 目录）或 IDE 内建 CMake 支持。VSCode 可安装 CMake Tools 与 clangd 插件。

**前端开发**建议单独启动 Vite dev server（带热更新），API 请求会代理到后端：

```bash
cd frontend && npm run dev
```

代理规则见 `frontend/vite.config.ts`：`/admin/api` → `http://localhost:7778`，`/admin/ws` → `ws://localhost:7778`。

### 仅检查前端类型

```bash
cd frontend && npm run type-check
```

## 运行

```bash
./build/insoulforge/exe/insoulforge
```

- HTTP 服务监听 **7778** 端口，管理后台：`http://localhost:7778/index.html`
- 数据目录 `data/` 与日志 `logs/bot.log` 在工作目录下生成
- 控制台输入 `quit` 优雅退出

注意：构建产物会输出到 `build/`，而 `data/`、`logs/` 与 `public/` 位于仓库根目录。开发时如果从仓库根目录运行
`./build/insoulforge/exe/insoulforge`，读取的是根目录的 `data/`；如果直接进入 `build/insoulforge/` 运行，则会在
`build/insoulforge/` 下生成数据目录。

## 项目结构

```
insoulforge/
├── CMakeLists.txt            # C++23 构建脚本（含前端自动构建）
├── include/                  # 头文件（按模块分目录）
│   ├── agent/
│   │   ├── runtime/          # Agent 编排：AgentSystem / RouterAgent / ExecutorAgent / AgentTypes
│   │   └── tools/            # 工具运行时、插件契约与 plugins/ 具体工具插件
│   ├── controllers/          # ProcessQQMessages / AdminController / AdminWebSocket / LogWebSocket / CommandHandler
│   ├── event/                # 领域事件：EventBus / 事件载荷 / subscribers 订阅者
│   ├── message/              # 消息链路：MessagePipeline / MessageContext / MessageMiddleware
│   │   └── middleware/       # 具体处理节点：事件、命令、会话、记录、Agent 与后处理
│   ├── config/               # Config：从数据库加载全部配置的单例
│   ├── model/                # 数据模型：QQMessage
│   ├── service/              # 服务层：MessageService / MemoryService / LlmClient / OneBotClient / LongTermMemory / ToolRegistry / ChatRecordManager / MemoryManager / SessionConfigManager 等
│   ├── storage/              # SQLite 存储：Database / 各领域存储命名空间（Session / ChatRecord / Memory / Affinity / Prompt / Admin / Config / Usage / Tool / Task）
│   └── util/                 # Log / Http / CommonUtil
├── src/                      # 源文件（与 include/ 一一对应，入口 main.cpp）
├── frontend/                 # Vue 3 + Vite + TypeScript 管理后台
│   └── src/components/       # 13 个功能组件（Dashboard、LLM配置、提示词、自定义工具、表情库、管理员、群管理、运行日志、请求调试、记忆与上下文、OneBot配置、用量统计、关于）
├── agentTools/               # 自定义工具的 JSON 配置（random / get_time / get_weather / search_web）
├── docs/                     # 文档
└── run.sh                    # 部署启动脚本
```

## 架构

### 消息处理流水线

```
OneBot HTTP POST /
    │
    ▼
ProcessQQMessages::receiveMessages
    │ 解析 JSON 并立即确认 HTTP 请求
    ▼
MessagePipeline
    │ 1. EventNormalization：普通消息直通；拍一拍归一化、记录或忽略
    │ 2. AgentAvailability：Agent 未运行时终止
    │ 3. MessageSetup：创建 QQMessage 和会话配置
    │ 4. Command：命令消息交给 CommandHandler 后终止
    │ 5. SessionEnabled：非命令消息的会话开关检查
    │ 6. FormatMessage：@ 转换、图片识别等高成本格式化
    │ 7. RecordMessage：写入聊天记录并发布 MessageRecordedEvent
    │ 8. AgentReply：调用两层 Agent 并按需发送文字回复
    │ 9. PostProcess：发布 MessageProcessingCompletedEvent
    ▼
AgentSystem::process
    │ 组内互斥（同群消息串行处理，防止上下文并发污染）
    ▼
Layer 1: RouterAgent
    │ 输入：最近聊天记录 + 长期记忆 + 群配置
    │ 输出 RouterDecision：SKIP / REPLY + 策略（语气、长度）
    ▼
Layer 2: ExecutorAgent
    │ 带工具调用循环生成回复
    │ deep_think 工具按需把复杂问题交给深度思考模型求解，答案作为工具结果回传后再组织回复
    ▼
MessageService::sendGroupMsg → OneBot API
```

关键数据结构见 `include/agent/runtime/AgentTypes.hpp`（`RouterDecision`、`ReplyDecision`）。

消息处理的几个约束：

- 命令先于启用检查处理，因此管理员可以在禁用会话中继续使用 `/enable`、`/status` 等管理命令。
- `QQMessage::formatMessage` 放在启用检查之后，避免禁用会话触发图片识别等 LLM 调用。
- 同一会话串行处理。普通消息处理期间到达的普通消息会跳过；@ 机器人、私聊、系统定时任务等高优先级消息会尝试打断正在处理的普通消息。高优先级消息之间不互相打断，只排队等待。
- 拍一拍 notice 不是普通消息；禁用会话直接跳过，已启用会话中再根据目标决定“合成普通消息”或“仅写聊天记录”。
- 中间件按 `MessageMiddlewareCatalog` 中的注册顺序执行；每个节点只负责一个阶段，并通过 `MessageFlow::Stop` 短路后续处理。
- 每个中间件调用均由 `MessagePipeline` 捕获异常；异常日志包含节点标识、会话 ID 和消息 ID，随后终止本次链路，不会再执行后续节点。

### 领域事件

`EventBus` 将消息主流程与非核心副作用解耦。事件发布者不依赖订阅者实现；订阅者按注册顺序执行，单个订阅者失败仅记录事件类型、
订阅者标识、会话 ID 与消息 ID，不会阻断其他订阅者或消息主处理。

- `MessageRecordedEvent`：消息记录写入者发布；`MessageWebSocketSubscriber` 推送管理后台消息。
- `MessageProcessingCompletedEvent`：`PostProcessMiddleware` 发布；`SessionStatisticsSubscriber` 更新会话统计，
  `MemoryMaintenanceSubscriber` 触发记忆提取与窗口滑动。

新增事件时，在 `include/event/DomainEvent.hpp` 添加强类型载荷，再实现 `EventSubscriber` 并在
`EventSubscriberCatalog` 显式注册。总线必须在消息链路初始化前完成 `EventBus::initialize()`。

新增消息处理阶段时，在 `include/message/middleware/` 与 `src/message/middleware/` 中实现一个 `MessageMiddleware`，
再将其加入 `MessageMiddlewareCatalog`。中间件标识必须全局唯一；`MessagePipeline` 会在初始化时校验内置节点，避免因空节点或
重复标识启动半初始化的处理链路。自定义节点仅可在 `MessagePipeline::initialize()` 之后、HTTP 服务开始接收请求前注册：
`addMiddleware()` 追加到末尾，`insertBefore(anchorId, ...)` 或 `insertAfter(anchorId, ...)` 按内置或已有节点标识定位插入。
例如在 `format_message` 前插入内容检查节点，或在 `record_message` 后插入审计节点。

### 工具系统

- `ToolRegistry` 以进程内插件管理工具，LLM 调用时按类别分组注入 prompt：
    - `REPLY`：回复工具（`reply` / `no_reply` / `reply_with_quote`），调用后结束本轮处理
    - `INFORMATION`：信息工具（`recall_memory` / `list_stickers` / `deep_think` / `list_scheduled_tasks`），获取数据
    - `ACTION`：动作工具（`send_face` / `send_image` / `send_sticker` / `save_sticker` / `rename_sticker` /
      `reply_and_continue` / `delete_sticker` / `at_user` / `ban_user` / `send_poke` / `recall_message` /
      `create_scheduled_task` / `cancel_scheduled_task`），执行操作
- 内置工具分为 `builtin.reply`、`builtin.info`、`builtin.action` 三个 `ToolPlugin` 实现，注册代码按类别拆分在
  `src/agent/tools/plugins/ReplyToolsPlugin.cpp` / `InfoToolsPlugin.cpp` / `ActionToolsPlugin.cpp`。`ToolPluginCatalog`
  显式组合并加载
  插件，`ToolRuntime` 不感知具体插件；自定义工具归属 `custom` 插件。重载某个插件只影响该插件的工具，工具名在
  全局唯一，冲突时拒绝注册，避免自定义工具覆盖内置工具后出现定义与执行不一致。
- 工具定义按“类别 → `promptOrder` → 工具名”稳定排序，避免重启或自定义工具刷新后改变 provider 的 prompt cache
  和模型偏好。私聊请求会在注入前排除 `GROUP_ONLY` 工具（当前为 `at_user`、`ban_user`、`send_poke`）。
- 自定义工具（Python 脚本 / HTTP 接口）存储于数据库，启动及后台刷新时加载；Python 工具通过 `sys.argv[1]` 传入参数 JSON 文件路径
- 拍一拍、撤回、引用回复、表情包收发、定时任务均由上述工具实现，由 Executor 根据上下文自动决策调用；天气、搜索、随机数、时间等能力来自
  `agentTools/` 目录的可导入自定义工具

工具执行的几个约束：

- `reply` / `reply_with_quote` / `no_reply` 是回复工具，调用后结束本轮处理；它们在 `ExecutorAgent::processToolCalls`
  中被拦截，不走普通工具返回值路径。
- `send_sticker` / `send_poke` / `reply_and_continue` 是中途动作，执行后本轮不结束，最终仍需用回复工具收尾；一次对话需要连续多条消息时，用
  `reply_and_continue` 发送前置消息，再用 `reply` 或 `no_reply` 收尾。
- `send_sticker` 与 `reply_and_continue` 通过 `MessageService` 直接发送，成功后自动写入聊天记录并推送
  WebSocket；主流程只负责发送最终文字回复。
- `deep_think` 是信息工具，不是全局思考模式。Executor 只在复杂问题需要额外推理时调用，工具结果再回到 Executor 组织成聊天回复。

### 记忆系统

- **短期记忆**：本地 SQLite（`MemoryManager`），按群存储
- **长期记忆**：本地 SQLite（`long_term_memory` 表，embedding 以 float 数组存 BLOB），由 `MemoryService` 负责提取、合并、去重与迁移，
  `LongTermMemory` 服务封装存取
- **提取机制**（可配置）：聊天记录窗口超过 `windowTriggerCount` 条时，LLM 从待删除的旧记录中提取记忆并滑动窗口（保留最近
  `windowKeepCount` 条）；Router 有独立的子窗口参数（`routerWindowTriggerCount` / `routerWindowKeepCount`）
- **归类机制**：每轮提取后，LLM 以新记忆召回相似长期记忆（阈值 `longTermRecallThreshold`）并整理归类为短期/长期两部分；
  新长期记忆逐条经 Embedding API 向量化写入长期记忆库，被合并取代的召回条目从库中删除
- 记忆提取与合并复用 executor 模型（`LlmClient::requestLLM`）
- 向量化使用独立的 embedding 配置（`LlmClient::requestEmbedding`）；`recall_memory` 工具按余弦相似度（阈值 0.3）检索长期记忆

### 配置系统

`Config` 单例从 SQLite 加载 LLM API 配置（router / executor / executorThinking / image / embedding，每组独立配置 model /
endpoint 等参数，可选 `reasoningEffort`）、QQ Bot 配置、记忆参数。管理后台另有 `memory`
LLM 配置项存储于数据库，但当前记忆提取复用 executor 模型。提示词由 `PromptService` 管理（`executor_system` /
`router_system`），支持 `{botName}` 占位符，修改后写回数据库。

**用量统计**：每次 LLM 调用通过 `LlmClient::logUsage` 记录模型与 token 用量，后台"用量统计"页读取 `/admin/api/usage` 展示。

### 数据库

SQLite 文件位于 `data/insoulforge.db`（`Database` 单例，`shared_mutex` 线程安全）。存储：聊天记录、短期/长期记忆、LLM
配置、提示词、启用群、管理员、表情、自定义工具。

调试时可用任意 SQLite 客户端查看：

```bash
sqlite3 data/insoulforge.db ".tables"
```

## 开发指南

### 添加 C++ 内置工具

小型工具可直接按类别编辑 `src/agent/tools/plugins/ReplyToolsPlugin.cpp` / `InfoToolsPlugin.cpp` /
`ActionToolsPlugin.cpp` 注册（共享的参数 Schema 辅助函数在 `include/agent/tools/ToolArgument.hpp`）。内置文件由对应的
`builtin.*` 插件加载，因此工具会自动归属到该插件：

```cpp
registry.registerTool(
    {
        .name = "my_tool",
        .description = "工具描述，LLM 据此判断何时调用",
        .parameters = paramsJson,   // JSON Schema 格式
        .handler = [](const json args, ToolCallContext ctx) -> drogon::Task<std::string> {
            co_return "结果";
        },
    }, ToolCategory::ACTION);
```

新增独立能力域时，实现 `ToolPlugin` 并在 `src/agent/tools/ToolPluginCatalog.cpp` 的实例列表中增加该类；不需要修改
`ToolRuntime`、`AgentSystem` 或 `ExecutorAgent`。插件 ID 应使用稳定、全小写的命名空间形式。插件重载只会先卸载
自己此前的工具，跨插件同名工具会被拒绝：

```cpp
class WeatherToolsPlugin final : public ToolPlugin {
public:
    std::string_view id() const noexcept override { return "weather"; }

    void registerTools(ToolRegistry &registry) const override {
        registry.registerTool(myWeatherTool, ToolCategory::INFORMATION);
    }
};

// ToolPluginCatalog.cpp
const WeatherToolsPlugin weatherTools;
// 将 &weatherTools 加入 plugins 指针列表
```

不要把“工具排在前面”当作调用策略。顺序对部分模型有弱影响，因此系统保证顺序稳定；更有效的做法是按会话能力筛选、合并重叠工具，并在名称、描述和参数
Schema 中写清楚触发条件与边界。需要调整同类别展示位置时才设置 `Tool::promptOrder`，数值越小越靠前。

### 添加自定义工具（Python）

1. 管理后台 → 自定义工具 → 添加
2. 填写名称、描述、参数定义（JSON Schema）、Python 脚本
3. 脚本从 `sys.argv[1]` 指定的 JSON 文件读取参数，结果打印到 stdout

也可直接编写 JSON 配置文件放入 `agentTools/` 后从后台导入，格式：

```json
{
  "name": "tool_name",
  "description": "工具描述",
  "parameters": {
    "type": "object",
    "properties": {
      "param1": {
        "type": "string",
        "description": "参数说明"
      }
    },
    "required": [
      "param1"
    ]
  },
  "scriptContent": "import json\nimport sys\nwith open(sys.argv[1]) as f:\n    args = json.load(f)\nprint(args['param1'])",
  "readme": "# 工具说明\n作者、用法、联系方式等"
}
```

### 添加管理 API

1. 在 `include/controllers/AdminController.hpp` 中声明路由与 handler
2. 在 `../controllers/AdminController.cpp` 中实现（协程写法 `Task<HttpResponsePtr>` + `co_return`）
3. 数据访问统一通过 `Database` 单例

现有路由以 `/admin/api/` 为前缀（聊天记录、LLM 配置、提示词、表情、群、管理员、自定义工具、记忆、QQ 配置、用量统计），新路由建议沿用该前缀。

### 添加前端页面

1. 在 `frontend/src/components/` 新建 Vue 组件
2. 在 `frontend/src/App.vue` 注册导航
3. API 请求路径以 `/admin/api` 开头（dev 模式自动代理到后端）

### 修改数据库表结构

数据库表由 `Database::initialize()` 创建。修改表结构时需注意现有用户的数据库不会自动迁移：请在 `initialize()` 中编写
`ALTER TABLE` 式的增量迁移（检测列/表是否存在再执行），而非仅修改建表语句。

## 调试

- **日志**：`spdlog` 输出到控制台与 `logs/bot.log`，模块间通过 `util/Logger.hpp` 封装。排查消息流水线问题时先看日志中
  Router 决策与 Executor 输出。管理后台读取 `LogBuffer` 的内存缓冲，启动时按 `bot.log`、`bot.1.log`、`bot.2.log` 从新到旧补足最近
  5000 条记录，避免全量解析滚动日志拖慢启动
- **协程**：所有异步 I/O 使用 `drogon::Task<T>` / `co_await`，注意 `co_await` 后对象生命周期（捕获 `shared_ptr` 而非裸指针）
- **组内互斥**：`AgentSystem` 保证同一群的消息串行处理，新增消息处理逻辑时不要绕过该机制
- **前端**：`npm run dev` + 浏览器 DevTools；后端日志会打印收到的 OneBot 原始 JSON

## 代码规范

完整规范见 [CODING_STYLE.md](./CODING_STYLE.md)，要点：

- 头文件 `.hpp`、源文件 `.cpp`、类 PascalCase、方法 camelCase、成员 `m_` 前缀、静态 `s_` 前缀
- 包含顺序：标准库 → 第三方 → 项目头文件
- 格式化使用 `.clang-format`（Google 风格，4 空格缩进，120 列），提交前建议运行 clang-format 与 clang-tidy
- 单例统一 `static ClassName& instance()` + 私有构造
