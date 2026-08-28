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

- 可执行文件 → `build/bot/exe/insoulforge`
- 前端静态文件 → `build/bot/public/`（CMake 会在前端源码变化时自动执行 `npm run build`）

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
./build/bot/exe/insoulforge
```

- HTTP 服务监听 **7778** 端口，管理后台：`http://localhost:7778/index.html`
- 数据目录 `data/` 与日志 `logs/bot.log` 在工作目录下生成
- 控制台输入 `quit` 优雅退出

注意：构建产物会输出到 `build/`，而 `data/`、`logs/` 与 `public/` 位于仓库根目录。开发时如果从仓库根目录运行
`./build/bot/exe/insoulforge`，读取的是根目录的 `data/`；如果直接进入 `build/bot/` 运行，则会在 `build/bot/` 下生成数据目录。

## 项目结构

```
insoulforge/
├── main.cpp                  # 入口：初始化日志/数据库/配置/Agent，启动 Drogon
├── CMakeLists.txt            # C++23 构建脚本（含前端自动构建）
├── controllers/              # Drogon HTTP 控制器（.h 为传统命名，勿改）
│   ├── ProcessQQMessages.*   # POST / 接收 OneBot 消息，驱动 Agent 流水线
│   ├── AdminController.*     # /admin/api/* 管理后台 REST API
│   └── AdminWebSocket.*      # /admin/ws 实时聊天记录推送
├── include/                  # 头文件（按模块分目录）
│   ├── agent/                # Agent 系统：AgentSystem / RouterAgent / ExecutorAgent / AgentToolManager / AgentTypes
│   ├── api/                  # ApiClient：封装 OpenAI 格式 LLM API 调用
│   ├── config/               # Config：从数据库加载全部配置的单例
│   ├── handler/              # CommandHandler：群聊 /xxx 命令
│   ├── model/                # 数据模型：QQMessage / ChatRecordManager / MemoryManager / GroupConfigManager
│   ├── service/              # 服务层：ToolRegistry / MemoryService / PromptService / RAGFlowClient / MessageService / WebSocketManager
│   ├── storage/              # Database：SQLite 单例
│   └── util/                 # Log / Prompt / tool.h
├── src/                      # 源文件（与 include/ 一一对应）
├── frontend/                 # Vue 3 + Vite + TypeScript 管理后台
│   └── src/components/       # 11 个管理页面（LLM配置、提示词、自定义工具、表情库、管理员、群管理、聊天记录、知识库配置、记忆与上下文、OneBot配置、用量统计）
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
    │ 1. 解析 JSON，校验 post_type == "message"
    │ 2. 命令消息（@Bot + /xxx）→ CommandHandler，不受群启用状态影响
    │ 3. 非命令消息：群未启用则丢弃
    │ 4. QQMessage::formatMessage 格式化（@ 转换、图片识别等）
    ▼
AgentSystem::process
    │ 组内互斥（同群消息串行处理，防止上下文并发污染）
    ▼
Layer 1: RouterAgent
    │ 输入：最近聊天记录 + 长期记忆 + 群配置
    │ 输出 RouterDecision：SKIP / REPLY + 策略（语气、长度、是否启用思考模式）
    ▼
Layer 2: ExecutorAgent
    │ 标准模式：带工具调用循环生成回复
    │ 思考模式：思考模型先分析 → 执行模型生成回复（两阶段）
    ▼
MessageService::sendGroupMsg → OneBot API
```

关键数据结构见 `include/agent/AgentTypes.hpp`（`RouterDecision`、`ReplyDecision`）。

### 工具系统

- `ToolRegistry` 按类别管理工具，LLM 调用时按类别分组注入 prompt：
    - `TERMINAL`：终结工具（`reply` / `no_reply` / `reply_with_quote`），调用后结束本轮处理
    - `INFORMATION`：信息工具（`search_knowledge` / `recall_memory` / `get_group_name` / `list_stickers`），获取数据
    - `ACTION`：动作工具（`send_face` / `send_image` / `send_sticker` / `save_sticker` / `rename_sticker` /
      `delete_sticker` / `at_user` / `ban_user` / `send_poke` / `recall_message`），执行操作
- 内置工具在 `src/agent/AgentToolManager.cpp` 注册；自定义工具注册为 `INFORMATION` 类别
- 自定义工具（Python 脚本 / HTTP 接口）存储于数据库，启动时加载；Python 工具通过 `sys.argv[1]` 传入参数 JSON 文件路径
- 拍一拍、撤回、引用回复、表情包收发均由上述工具实现，由 Executor 根据上下文自动决策调用；天气、搜索、随机数、时间等能力来自
  `agentTools/` 目录的可导入自定义工具

### 记忆系统

- **短期记忆**：本地 SQLite（`MemoryManager`），按群存储
- **长期记忆**：RAGFlow 记忆库，由 `MemoryService` 负责提取、合并、去重与迁移
- **提取机制**（可配置）：聊天记录窗口超过 `windowTriggerCount` 条时，LLM 从待删除的旧记录中提取记忆并滑动窗口（保留最近
  `windowKeepCount` 条）；Router 有独立的子窗口参数（`routerWindowTriggerCount` / `routerWindowKeepCount`）
- **迁移机制**：短期记忆超过 `shortTermMemoryMax` 条时，LLM 筛选 `memoryMigrateCount` 条重要记忆写入 RAGFlow
- 记忆提取与合并复用 executor 模型（`ApiClient::requestLLM`）

### 配置系统

`Config` 单例从 SQLite 加载 LLM API 配置（router / executor / executorThinking / image，每组独立配置 model / endpoint /
temperature 等参数，可选 `reasoningEffort`）、知识库（RAGFlow，含 `enabled` 开关）、QQ Bot 配置、记忆参数。管理后台另有 `memory`
LLM 配置项存储于数据库，但当前记忆提取复用 executor 模型。提示词由 `PromptService` 管理（`executor_system` /
`router_system`），支持 `{botName}` 占位符，修改后写回数据库。

**用量统计**：每次 LLM 调用通过 `ApiClient::logUsage` 记录模型与 token 用量，后台"用量统计"页读取 `/admin/api/usage` 展示。

### 数据库

SQLite 文件位于 `data/insoulforge.db`（`Database` 单例，`shared_mutex` 线程安全）。存储：聊天记录、短期/长期记忆、LLM
配置、提示词、启用群、管理员、表情、自定义工具。

调试时可用任意 SQLite 客户端查看：

```bash
sqlite3 data/insoulforge.db ".tables"
```

## 开发指南

### 添加 C++ 内置工具

编辑 `src/agent/AgentToolManager.cpp`，在 `initialize()` 中注册：

```cpp
registry.registerTool(
    {
        .name = "my_tool",
        .description = "工具描述，LLM 据此判断何时调用",
        .parameters = paramsJson,   // JSON Schema 格式
        .handler = [](const Json::Value &args) -> drogon::Task<std::string> {
            co_return "结果";
        }
    }, ToolCategory::ACTION);
```

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
    "properties": { "param1": { "type": "string", "description": "参数说明" } },
    "required": ["param1"]
  },
  "scriptContent": "import json\nimport sys\nwith open(sys.argv[1]) as f:\n    args = json.load(f)\nprint(args['param1'])",
  "readme": "# 工具说明\n作者、用法、联系方式等"
}
```

### 添加管理 API

1. 在 `../controllers/AdminController.h` 中声明路由与 handler
2. 在 `../controllers/AdminController.cpp` 中实现（协程写法 `Task<HttpResponsePtr>` + `co_return`）
3. 数据访问统一通过 `Database` 单例

现有路由以 `/admin/api/` 为前缀（聊天记录、LLM 配置、提示词、表情、群、管理员、自定义工具、记忆、知识库、QQ 配置、用量统计），新路由建议沿用该前缀。

### 添加前端页面

1. 在 `frontend/src/components/` 新建 Vue 组件
2. 在 `frontend/src/App.vue` 注册导航
3. API 请求路径以 `/admin/api` 开头（dev 模式自动代理到后端）

### 修改数据库表结构

数据库表由 `Database::initialize()` 创建。修改表结构时需注意现有用户的数据库不会自动迁移：请在 `initialize()` 中编写
`ALTER TABLE` 式的增量迁移（检测列/表是否存在再执行），而非仅修改建表语句。

## 调试

- **日志**：`spdlog` 输出到控制台与 `logs/bot.log`，模块间通过 `util/Log.hpp` 封装。排查消息流水线问题时先看日志中 Router
  决策与 Executor 输出
- **协程**：所有异步 I/O 使用 `drogon::Task<T>` / `co_await`，注意 `co_await` 后对象生命周期（捕获 `shared_ptr` 而非裸指针）
- **组内互斥**：`AgentSystem` 保证同一群的消息串行处理，新增消息处理逻辑时不要绕过该机制
- **前端**：`npm run dev` + 浏览器 DevTools；后端日志会打印收到的 OneBot 原始 JSON

## 代码规范

完整规范见 [CODING_STYLE.md](./CODING_STYLE.md)，要点：

- 头文件 `.hpp`、源文件 `.cc`、类 PascalCase、方法 camelCase、成员 `m_` 前缀、静态 `s_` 前缀
- 控制器头文件沿用传统 `.h` 命名（历史原因）
- 包含顺序：标准库 → 第三方 → 项目头文件
- 格式化使用 `.clang-format`（Google 风格，4 空格缩进，120 列），提交前建议运行 clang-format 与 clang-tidy
- 单例统一 `static ClassName& instance()` + 私有构造