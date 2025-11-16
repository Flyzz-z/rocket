#ifndef ROCKET_NET_RPC_RPC_CHANNEL_H
#define ROCKET_NET_RPC_RPC_CHANNEL_H

#include "rocket/net/tcp/tcp_client.h"
#include <google/protobuf/service.h>
#include <memory>
#include <asio/steady_timer.hpp>

namespace rocket {

#define NEWMESSAGE(type, var_name)                                             \
  std::shared_ptr<type> var_name = std::make_shared<type>();

#define NEWRPCCONTROLLER(var_name)                                             \
  std::shared_ptr<rocket::RpcController> var_name =                            \
      std::make_shared<rocket::RpcController>();

#define NEWRPCCHANNEL(addr, var_name)                                          \
  std::shared_ptr<rocket::RpcChannel> var_name =                               \
      std::make_shared<rocket::RpcChannel>(                                    \
          rocket::RpcChannel::FindAddr(addr));

#define CALLRPRC(addr, stub_name, method_name, controller, request, response,  \
                 closure)                                                      \
  {                                                                            \
    NEWRPCCHANNEL(addr, channel);                                              \
    channel->Init(controller, request, response, closure);                     \
    stub_name(channel.get())                                                   \
        .method_name(controller.get(), request.get(), response.get(),          \
                     closure.get());                                           \
  }

class RpcChannel : public google::protobuf::RpcChannel,
                   public std::enable_shared_from_this<RpcChannel> {

public:
  typedef std::shared_ptr<RpcChannel> s_ptr;
  typedef std::shared_ptr<google::protobuf::RpcController> controller_s_ptr;
  typedef std::shared_ptr<google::protobuf::Message> message_s_ptr;
  typedef std::shared_ptr<google::protobuf::Closure> closure_s_ptr;

public:
  // 获取 addr
  static std::vector<tcp::endpoint> FindAddr(const std::string &str);

public:
  RpcChannel(std::vector<tcp::endpoint> peer_addrs);

  ~RpcChannel();

  void Init(controller_s_ptr controller, message_s_ptr req, message_s_ptr res,
            closure_s_ptr done);

  void CallMethod(const google::protobuf::MethodDescriptor *method,
                  google::protobuf::RpcController *controller,
                  const google::protobuf::Message *request,
                  google::protobuf::Message *response,
                  google::protobuf::Closure *done) override;

  google::protobuf::RpcController *getController();

  google::protobuf::Message *getRequest();

  google::protobuf::Message *getResponse();

  google::protobuf::Closure *getClosure();

  TcpClient *getTcpClient();

private:
  void callBack();

private:
  controller_s_ptr controller_{nullptr};
  message_s_ptr request_{nullptr};
  message_s_ptr response_{nullptr};
  closure_s_ptr closure_{nullptr};

  bool is_init_{false};

  std::vector<tcp::endpoint> peer_addrs_;
	int addr_index_{0};
  tcp::endpoint local_addr_;
  TcpClient::s_ptr client_;
  int client_id_;

  // 用于存储超时定时器，以便在收到响应时取消
  std::shared_ptr<asio::steady_timer> timeout_timer_;

};

} // namespace rocket

#endif