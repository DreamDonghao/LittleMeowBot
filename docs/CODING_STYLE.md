# insoulforge 项目编码规范

## 命名规范

### 文件命名

- **头文件**: `PascalCase.hpp` (如 `MemoryManager.hpp`, `LlmClient.hpp`)
- **源文件**: `PascalCase.cpp` (如 `ProcessQQMessages.cpp`)
- **控制器**: `PascalCase.hpp/.cpp` (如 `AdminController.hpp`)

### 类命名

- **PascalCase**: `MemoryManager`, `LlmClient`, `AgentSystem`
- **接口/抽象类**: 以 `I` 开头 (如 `IHandler`)
- **异常类**: 以 `Exception` 结尾 (如 `ConfigException`)

### 函数/方法命名

- **camelCase**: `getMemory()`, `processMessage()`, `sendMessage()`
- **getter**: `getName()`, `getGroupId()`
- **setter**: `setName()`, `setGroupId()`
- **bool 返回**: `isXxx()`, `hasXxx()`, `canXxx()` (如 `isEnabled()`, `hasConfig()`)

### 变量命名

- **局部变量**: `camelCase` (如 `groupId`, `messageCount`)
- **成员变量**: `m_` 前缀 + camelCase (如 `m_groupId`, `m_config`)
- **静态变量**: `s_` 前缀 + camelCase (如 `s_instance`)
- **常量**: `UPPER_SNAKE_CASE` (如 `MAX_RETRY_COUNT`)
- **全局变量**: `g_` 前缀 + camelCase (如 `g_newQQMesCounts`)

### 枚举命名

- **枚举类型**: `PascalCase` (如 `ToolCategory`)
- **枚举值**: `UPPER_SNAKE_CASE` 或 `PascalCase`

### 命名空间

- **PascalCase**: `insoulforge`

### 宏命名

- **UPPER_SNAKE_CASE**: `INSOULFORGE_MEMORY_MANAGER_HPP`

## Doxygen 注释规范

### 文件注释

```cpp
/// @file MemoryManager.hpp
/// @brief 短期记忆管理器
/// @author donghao
/// @date 2026-04-02
```

### 类注释

```cpp
/// @brief 短期记忆管理类
/// @details 管理单个群组的短期记忆，支持存储和检索操作
class MemoryManager {
    // ...
};
```

### 方法注释

```cpp
/// @brief 获取群的短期记忆
/// @param groupId 群号
/// @return 短期记忆文本，每行一条记忆条目
[[nodiscard]] std::string getMemory(uint64_t groupId) const;
```

### 成员变量注释

```cpp
uint64_t m_groupId;  ///< 群号
std::string m_name;  ///< 群名称
```

## 代码风格

### 头文件保护

使用 `#pragma once` 而非传统宏保护。

### 包含顺序

1. 标准库头文件
2. 第三方库头文件
3. 项目内部头文件

```cpp
#include <string>
#include <vector>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

#include <config/Config.hpp>
#include <storage/Database.hpp>
```

### 自动格式化

建议使用 clang-format，配置参考：

```yaml
BasedOnStyle: Google
IndentWidth: 4
ColumnLimit: 120
```

## 协程参数规范（drogon::Task）

协程的形参存储在**协程帧**中，帧的生命周期独立于调用方栈帧：协程可能在调用方返回之后才被恢复（`async_run` 后台任务、跨线程调度等）。引用/指针形参只在帧里保存一个地址，被引用对象若先于协程结束销毁即悬垂；按值形参在协程创建时把值**复制/移动进帧**，帧独立持有数据。

此外，clang 21 实现 CWG2814（promise 以协程实参参与构造）后，drogon `Task<T>` 的 promise 是聚合类型（首成员 `std::optional<T> value`），会被第一个协程实参聚合初始化——json 的隐式转换运算符曾使 `promise{json}` 变得可行，对非字符串 json 抛 `type_error.302`，在半构造协程帧内异常展开导致 SIGSEGV（drogon issue #2579，上游修复 PR #2580 为 promise 加 `promise_type() = default;` 使其非聚合）。本项目已在 CMake 全局定义 `JSON_USE_IMPLICIT_CONVERSIONS=0`，json 不再具有任何隐式转换路径，该误初始化对 json 实参不可行（最小复现验证：宏=1 时 `const json&` 实参启动即崩，宏=0 时引用/按值/指针全部正常）。

规则：

1. **`drogon::Task` 协程的值语义参数（json、std::string、容器、结构体）一律按值传递**，不得以引用/指针形式参传入。
2. **调用方在调用之后不再使用实参时，必须 `std::move`**（如 `co_await send(std::move(body))`，协程帧零拷贝接管）；仍需继续使用时传 lvalue（复制进帧）。**禁止从单例、配置成员或 lambda 捕获中 move**——move 会破坏其后续使用；工具 handler 这类会被多次调用的 lambda 协程，捕获永远按复制传递。
3. **普通（非协程）函数不受此约束**：入参仍用 `const T&`；消费型函数（实参嵌入结果后不再使用）也按值 + 内部 `std::move`（如 `buildChatRequestBody`、`convertAtToCQCode`）。
4. **允许 `const&` 的协程参数例外**（引用对象生命周期明确覆盖协程全程）：
   - 进程级单例及其成员（如 `const LLMApiConfig &apiConfig` 来自 `Config` 单例）；
   - 不可复制的会话级管理器（`ChatRecordManager`、`MemoryManager`）——前提是调用方 `co_await` 到协程完成，不得 fire-and-forget；
   - `std::string_view` 形参只接受生命周期覆盖协程全程的实参（字符串字面量、静态字符串）。
5. 框架固定的签名（drogon 控制器 handler、`std::function<void(const HttpResponsePtr&)>` 回调）无法修改形参类别；此类协程如需把数据带过挂起点，在函数体入口先复制进局部变量。
6. 跨协程的**双向/输出参数不得用 `T&`**：改为按值传入、随返回值移出（如 `processToolCalls` 以 `std::tuple` 返回回传工具结果后的 messages 与累积的 CQ 码）。