/// @file MessageEventContractTests.cpp
/// @brief 消息链路与领域事件的契约测试

#include <drogon/utils/coroutine.h>
#include <event/EventBus.hpp>
#include <exception>
#include <iostream>
#include <message/MessageMiddleware.hpp>
#include <message/MessagePipeline.hpp>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    int failures = 0;

    void check(const bool condition, const std::string_view expression, const std::string_view testName) {
        if (!condition) {
            std::cerr << "[FAIL] " << testName << ": " << expression << '\n';
            ++failures;
        }
    }

#define CHECK(testName, expression) check((expression), #expression, testName)

    class RecordingMiddleware final : public insoulforge::MessageMiddleware {
    public:
        RecordingMiddleware(std::string id, std::vector<std::string> &trace,
          const insoulforge::MessageFlow flow = insoulforge::MessageFlow::Continue, const bool throws = false)
            : m_id(std::move(id)), m_trace(trace), m_flow(flow), m_throws(throws) {}

        [[nodiscard]] std::string_view id() const noexcept override { return m_id; }

        drogon::Task<insoulforge::MessageFlow> handle(insoulforge::MessageContext &) const override {
            m_trace.push_back(m_id);
            if (m_throws) {
                throw std::runtime_error("expected middleware failure");
            }
            co_return m_flow;
        }

    private:
        std::string m_id;
        std::vector<std::string> &m_trace;
        insoulforge::MessageFlow m_flow;
        bool m_throws;
    };

    [[nodiscard]] std::unique_ptr<RecordingMiddleware> recordingMiddleware(
      std::string id, std::vector<std::string> &trace,
      const insoulforge::MessageFlow flow = insoulforge::MessageFlow::Continue, const bool throws = false) {
        return std::make_unique<RecordingMiddleware>(std::move(id), trace, flow, throws);
    }

    void testMiddlewareInsertionOrder() {
        constexpr std::string_view kTestName = "middleware insertion order";
        std::vector<std::string> trace;
        insoulforge::MessagePipeline pipeline;
        std::vector<std::unique_ptr<insoulforge::MessageMiddleware>> middlewares;
        middlewares.push_back(recordingMiddleware("first", trace));
        middlewares.push_back(recordingMiddleware("anchor", trace));
        middlewares.push_back(recordingMiddleware("last", trace));
        pipeline.initialize(std::move(middlewares));
        pipeline.insertBefore("anchor", recordingMiddleware("before", trace));
        pipeline.insertAfter("anchor", recordingMiddleware("after", trace));

        drogon::sync_wait(pipeline.process(insoulforge::json::object()));
        CHECK(kTestName, trace == std::vector<std::string>({"first", "before", "anchor", "after", "last"}));
    }

    void testMiddlewareStopShortCircuits() {
        constexpr std::string_view kTestName = "middleware stop short circuits";
        std::vector<std::string> trace;
        insoulforge::MessagePipeline pipeline;
        std::vector<std::unique_ptr<insoulforge::MessageMiddleware>> middlewares;
        middlewares.push_back(recordingMiddleware("first", trace));
        middlewares.push_back(recordingMiddleware("stop", trace, insoulforge::MessageFlow::Stop));
        middlewares.push_back(recordingMiddleware("last", trace));
        pipeline.initialize(std::move(middlewares));

        drogon::sync_wait(pipeline.process(insoulforge::json::object()));
        CHECK(kTestName, trace == std::vector<std::string>({"first", "stop"}));
    }

    void testMiddlewareExceptionStopsChain() {
        constexpr std::string_view kTestName = "middleware exception stops chain";
        std::vector<std::string> trace;
        insoulforge::MessagePipeline pipeline;
        std::vector<std::unique_ptr<insoulforge::MessageMiddleware>> middlewares;
        middlewares.push_back(recordingMiddleware("first", trace));
        middlewares.push_back(recordingMiddleware("failing", trace, insoulforge::MessageFlow::Continue, true));
        middlewares.push_back(recordingMiddleware("last", trace));
        pipeline.initialize(std::move(middlewares));

        drogon::sync_wait(pipeline.process(insoulforge::json::object()));
        CHECK(kTestName, trace == std::vector<std::string>({"first", "failing"}));
    }

    void testEventSubscriberExceptionDoesNotStopDispatch() {
        constexpr std::string_view kTestName = "event subscriber exception isolation";
        std::vector<std::string> trace;
        insoulforge::EventBus eventBus;
        eventBus.initialize([&trace](insoulforge::EventBus &bus) {
            bus.subscribe<insoulforge::MessageRecordedEvent>(
              "first", [&trace](const insoulforge::MessageRecordedEvent &) -> drogon::Task<> {
                  trace.push_back("first");
                  co_return;
              });
            bus.subscribe<insoulforge::MessageRecordedEvent>(
              "failing", [&trace](const insoulforge::MessageRecordedEvent &) -> drogon::Task<> {
                  trace.push_back("failing");
                  throw std::runtime_error("expected subscriber failure");
                  co_return;
              });
            bus.subscribe<insoulforge::MessageRecordedEvent>(
              "last", [&trace](const insoulforge::MessageRecordedEvent &) -> drogon::Task<> {
                  trace.push_back("last");
                  co_return;
              });
        });

        drogon::sync_wait(eventBus.publish(insoulforge::MessageRecordedEvent{
          .sessionId = 42,
          .messageId = 7,
          .role = insoulforge::MessageRole::User,
          .recordContent = "record",
          .displayContent = "display",
        }));
        CHECK(kTestName, trace == std::vector<std::string>({"first", "failing", "last"}));
    }
} // namespace

int main() {
    testMiddlewareInsertionOrder();
    testMiddlewareStopShortCircuits();
    testMiddlewareExceptionStopsChain();
    testEventSubscriberExceptionDoesNotStopDispatch();

    if (failures == 0) {
        std::cout << "All message and event contract tests passed\n";
        return 0;
    }
    std::cerr << failures << " contract test(s) failed\n";
    return 1;
}
