#include <controllers/ProcessQQMessages.hpp>
#include <message/MessagePipeline.hpp>

using namespace insoulforge;
using namespace drogon;

Task<> ProcessQQMessages::receiveMessages(
  const HttpRequestPtr req, std::function<void(const HttpResponsePtr &)> callback) {
    auto body = parseJsonBody(req);
    if (!body) {
        auto resp = HttpResponse::newHttpResponse();
        resp->setStatusCode(k400BadRequest);
        resp->setBody("Invalid JSON or not an object");
        callback(resp);
        co_return;
    }
    // 返回响应（拍一拍等事件的处理放在应答之后，昵称补齐等异步操作不阻塞上报方）
    json respJson;
    respJson["status"] = "ok";
    callback(jsonResponse(respJson));

    co_await MessagePipeline::instance().process(std::move(*body));
}
