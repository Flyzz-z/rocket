#include "rocket/common/config.h"
#include "rocket/common/log.h"
#include "rocket/net/rpc/etcd_registry.h"
#include "rocket/net/rpc/rpc_channel.h"
#include "rocket/net/rpc/rpc_closure.h"
#include <arpa/inet.h>
#include <asio/awaitable.hpp>
#include <assert.h>
#include <fcntl.h>
#include <google/protobuf/service.h>
#include <memory>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "order.pb.h"
void test_rpc_channel() {

  // NEWRPCCHANNEL("127.0.0.1:12345", channel);
  NEWRPCCHANNEL("Order", channel);

  // std::shared_ptr<makeOrderRequest> request =
  // std::make_shared<makeOrderRequest>();

  NEWMESSAGE(makeOrderRequest, request);
  NEWMESSAGE(makeOrderResponse, response);

  request->set_price(100);
  request->set_goods("apple");

  NEWRPCCONTROLLER(controller);
  controller->SetMsgId("99998888");
  controller->SetTimeout(10000);

  std::shared_ptr<rocket::RpcClosure> closure =
      std::make_shared<rocket::RpcClosure>(nullptr, [request, response, channel,
                                                     controller]() mutable {
        if (controller->GetErrorCode() == 0) {
          INFOLOG("call rpc success, request[%s], response[%s]",
                  request->ShortDebugString().c_str(),
                  response->ShortDebugString().c_str());
          // 执行业务逻辑
          if (response->order_id() == "xxx") {
            // xx
          }
        } else {
          ERRORLOG(
              "call rpc failed, request[%s], error code[%d], error info[%s]",
              request->ShortDebugString().c_str(), controller->GetErrorCode(),
              controller->GetErrorInfo().c_str());
        }

        INFOLOG("now exit eventloop");
        // channel->getTcpClient()->stop();
        channel.reset();
      });

  {
    channel->Init(controller, request, response, closure);
    Order_Stub(channel.get())
        .makeOrder(controller.get(), request.get(), response.get(),
                   closure.get());
  }

  // CALLRPRC("127.0.0.1:12345", Order_Stub, makeOrder, controller, request,
  // response, closure);
}

int main() {

  rocket::Config::SetGlobalConfig(NULL);

  rocket::Logger::InitGlobalLogger(0);

  rocket::EtcdRegistry::init("127.0.0.1",2379, "root", "123456");

  // test_tcp_client();
  test_rpc_channel();
  sleep(10);

  INFOLOG("test_rpc_channel end");

  return 0;
}