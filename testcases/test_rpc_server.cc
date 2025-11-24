#include "proto/order.pb.h"
#include "rocket/common/config.h"
#include "rocket/logger/log.h"
#include "rocket/net/rpc/etcd_registry.h"
#include "rocket/net/rpc/rpc_dispatcher.h"
#include "rocket/net/tcp/tcp_server.h"
#include <arpa/inet.h>
#include <asio/ip/address.hpp>
#include <assert.h>
#include <fcntl.h>
#include <google/protobuf/service.h>
#include <memory>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

class OrderImpl : public Order {
public:
  void makeOrder(google::protobuf::RpcController *controller,
                 const ::makeOrderRequest *request,
                 ::makeOrderResponse *response,
                 ::google::protobuf::Closure *done) {
    if (request->price() < 10) {
      response->set_ret_code(-1);
      response->set_res_info("short balance");
      return;
    }
    response->set_order_id("20230514");
    if (done) {
      done->Run();
      delete done;
      done = NULL;
    }
  }
};

int main(int argc, char *argv[]) {

  if (argc != 2) {
    printf("Start test_rpc_server error, argc not 2 \n");
    printf("Start like this: \n");
    printf("./test_rpc_server ../conf/rocket.xml \n");
    return 0;
  }

  rocket::Config::SetGlobalConfig(argv[1]);

  rocket::Logger::InitGlobalLogger();

  // 服务端初始化: 从配置文件读取并注册所有提供的服务
  rocket::EtcdRegistry::initAsServerFromConfig();

  std::shared_ptr<OrderImpl> service = std::make_shared<OrderImpl>();
  // 注册服务实现到RPC分发器
  rocket::RpcDispatcher::GetRpcDispatcher()->registerService(service);

  asio::ip::address addr = asio::ip::address::from_string("192.168.124.128");
  asio::ip::tcp::endpoint endpoint =
      asio::ip::tcp::endpoint(addr, rocket::Config::GetGlobalConfig()->port_);

  rocket::TcpServer tcp_server(endpoint);
  tcp_server.start();

  return 0;
}