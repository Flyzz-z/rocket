#include <iostream>
#include "rocket/common/config.h"
#include "rocket/common/log.h"
#include "rocket/net/event_loop.h"
#include "rocket/net/rpc/etcd_registry.h"
#include "rocket/net/rpc/rpc_channel.h"
#include <arpa/inet.h>
#include <asio/awaitable.hpp>
#include <assert.h>
#include <cstddef>
#include <fcntl.h>
#include <google/protobuf/service.h>
#include <memory>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

#include "proto/co_stub/co_order_stub.h"
#include "rpc_controller.h"

asio::awaitable<void> test_rpc_channel() {
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

  channel->Init(controller, request, response, nullptr);
  co_await CoOrderStub(channel.get())
      .coMakeOrder(controller.get(), request.get(), response.get(), nullptr);
  if (!controller->Failed()) {
    std::cout << "response order id: " << response->order_id()
              << " ,res info " << response->res_info() << std::endl;
  } else {
    std::cout << "controller failed, error_code: " << controller->GetErrorCode()
              << ", error_info: " << controller->GetErrorInfo() << std::endl;
  }
  
  // 确保日志被刷新
  rocket::Logger::GetGlobalLogger()->flush();
}

int main() {

  rocket::Config::SetGlobalConfig(NULL);
  rocket::Logger::InitGlobalLogger(1); // 启用异步日志处理器
  rocket::EtcdRegistry::init("127.0.0.1", 2379, "root", "123456");

  rocket::EventLoop* event_loop = rocket::EventLoop::getThreadEventLoop();
  event_loop->addCoroutine(test_rpc_channel);
  event_loop->run();

  // 停止etcd watcher以避免gRPC断言错误
  rocket::EtcdRegistry::GetInstance()->stopWatcher();
  
  // 等待一段时间确保所有异步操作完成
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  std::cout << "test_rpc_channel end" << std::endl;

  return 0;
}