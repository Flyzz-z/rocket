#include "rocket/net/rpc/rpc_channel.h"
#include "rocket/common/config.h"
#include "rocket/common/error_code.h"
#include "rocket/logger/log.h"
#include "rocket/common/msg_id_util.h"
#include "rocket/common/run_time.h"
#include "rocket/net/coder/tinypb_protocol.h"
#include "rocket/net/event_loop.h"
#include "rocket/net/rpc/etcd_registry.h"
#include "rocket/net/rpc/rpc_controller.h"
#include "rocket/net/tcp/tcp_client.h"
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/address.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>
#include <asio/redirect_error.hpp>
#include <cstddef>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>

namespace rocket {

RpcChannel::RpcChannel(std::vector<tcp::endpoint> peer_addrs)
    : peer_addrs_(peer_addrs) {
  DEBUGLOG("RpcChannel");
}

RpcChannel::~RpcChannel() { DEBUGLOG("~RpcChannel"); }

void RpcChannel::callBack() {
  RpcController *my_controller = dynamic_cast<RpcController *>(getController());
  // 如果finsh直接返回，即便rpc返回结果，也会返回
  if (my_controller->Finished()) {
    return;
  }

  // 取消超时定时器
  if (timeout_timer_) {
    timeout_timer_->cancel();
    timeout_timer_.reset();
  }

  if (closure_) {
    closure_->Run();
  }

  my_controller->SetFinished(true);
  // 通知等待结果协程，rpc调用已完成
  if (my_controller->GetWaiter()) {
    my_controller->GetWaiter()->cancel();
  }
}

/*
        改造思路，启动协程去做，完成后调用done即可
*/
void RpcChannel::CallMethod(const google::protobuf::MethodDescriptor *method,
                            google::protobuf::RpcController *controller,
                            const google::protobuf::Message *request,
                            google::protobuf::Message *response,
                            google::protobuf::Closure *done) {

  std::shared_ptr<rocket::TinyPBProtocol> req_protocol =
      std::make_shared<rocket::TinyPBProtocol>();

  // 获取controller
  RpcController *my_controller = dynamic_cast<RpcController *>(controller);
  if (my_controller == NULL || request == NULL || response == NULL) {
    ERRORLOG("failed callmethod, RpcController convert error");
    my_controller->SetError(ERROR_RPC_CHANNEL_INIT,
                            "controller or request or response NULL");
    callBack();
    return;
  }

  // index归0
  if (addr_index_ >= peer_addrs_.size()) {
    addr_index_ = 0;
  }

  tcp::endpoint peer_addr;
  for (int i = 0; i < peer_addrs_.size(); i++) {
    if (peer_addrs_[addr_index_].address().is_unspecified()) {
      addr_index_ = (addr_index_ + 1) % peer_addrs_.size();
      continue;
    } else {
      peer_addr = peer_addrs_[addr_index_];
      break;
    }
  }

  if (peer_addr.address().is_unspecified()) {
    ERRORLOG("failed get peer addr");
    my_controller->SetError(ERROR_RPC_PEER_ADDR, "peer addr nullptr");
    callBack();
    return;
  }

  client_ = std::make_shared<TcpClient>(peer_addr);

  // 设置msg_id
  if (my_controller->GetMsgId().empty()) {
    // 先从 runtime 里面取, 取不到再生成一个
    // 这样的目的是为了实现 msg_id 的透传，假设服务 A 调用了 B，那么同一个 msgid
    // 可以在服务 A 和 B 之间串起来，方便日志追踪
    std::string msg_id = RunTime::GetRunTime()->msgid_;
    if (!msg_id.empty()) {
      req_protocol->msg_id_ = msg_id;
      my_controller->SetMsgId(msg_id);
    } else {
      req_protocol->msg_id_ = MsgIDUtil::GenMsgID();
      my_controller->SetMsgId(req_protocol->msg_id_);
    }

  } else {
    // 如果 controller 指定了 msgno, 直接使用
    req_protocol->msg_id_ = my_controller->GetMsgId();
  }

  // 设置method_name
  req_protocol->method_name_ = method->full_name();
  DEBUGLOG("%s | call method name [%s]", req_protocol->msg_id_.c_str(),
          req_protocol->method_name_.c_str());

  if (!is_init_) {
    std::string err_info = "RpcChannel not call init()";
    my_controller->SetError(ERROR_RPC_CHANNEL_INIT, err_info);
    ERRORLOG("%s | %s, RpcChannel not init ", req_protocol->msg_id_.c_str(),
             err_info.c_str());
    callBack();
    return;
  }

  // requeset 的序列化
  if (!request->SerializeToString(&(req_protocol->pb_data_))) {
    std::string err_info = "failde to serialize";
    my_controller->SetError(ERROR_FAILED_SERIALIZE, err_info);
    ERRORLOG("%s | %s, origin requeset [%s] ", req_protocol->msg_id_.c_str(),
             err_info.c_str(), request->ShortDebugString().c_str());
    callBack();
    return;
  }

  s_ptr channel = shared_from_this();

  // 获取事件循环
  EventLoop *event_loop = client_->getEventLoop();
  if (!event_loop) {
    ERRORLOG("RpcChannel::CallMethod event_loop nullptr");
    my_controller->SetError(ERROR_RPC_CHANNEL_INIT, "event_loop nullptr");
    callBack();
    return;
  }

  // 创建超时定时器并保存引用
  timeout_timer_ = std::make_shared<asio::steady_timer>(
      *event_loop->getIOContext(),
      std::chrono::milliseconds(my_controller->GetTimeout()));

  // 使用协程实现超时控制
  auto timeout_timer = timeout_timer_;  // 捕获shared_ptr副本
  event_loop->addCoroutine([my_controller, channel, timeout_timer]() mutable -> asio::awaitable<void> {
    asio::error_code ec;
    co_await timeout_timer->async_wait(asio::redirect_error(asio::use_awaitable, ec));

    // 如果定时器被取消（收到响应），直接返回
    if (ec == asio::error::operation_aborted) {
      INFOLOG("%s | timeout timer cancelled, rpc already completed",
              my_controller->GetMsgId().c_str());
      channel.reset();
      co_return;
    }

    INFOLOG("%s | call rpc timeout arrive",
            my_controller->GetMsgId().c_str());

    if (my_controller->Finished()) {
      channel.reset();
      co_return;
    }

    my_controller->StartCancel();
    my_controller->SetError(
        ERROR_RPC_CALL_TIMEOUT,
        "rpc call timeout " + std::to_string(my_controller->GetTimeout()));

    channel->callBack();
    channel.reset();
  });

  event_loop->addCoroutine([req_protocol, my_controller,
                            channel]() mutable -> asio::awaitable<void> {
    co_await channel->client_->connect();

    if (channel->getTcpClient()->getConnectErrorCode() != 0) {
      my_controller->SetError(channel->getTcpClient()->getConnectErrorCode(),
                              channel->getTcpClient()->getConnectErrorInfo());
      ERRORLOG(
          "%s | connect error, error coode[%d], error info[%s], peer addr[%s]",
          req_protocol->msg_id_.c_str(), my_controller->GetErrorCode(),
          my_controller->GetErrorInfo().c_str(),
          channel->getTcpClient()->getPeerAddr().address().to_string().c_str());

      channel->callBack();
      co_return;
    }

    DEBUGLOG("%s | connect success, peer addr[%s], local addr[%s]",
            req_protocol->msg_id_.c_str(),
            channel->getTcpClient()->getPeerAddr().address().to_string().c_str(),

            channel->getTcpClient()->getLocalAddr().address().to_string().c_str());

    DEBUGLOG("client make write message");
    channel->getTcpClient()->writeMessage(
        req_protocol, [req_protocol,channel](AbstractProtocol::s_ptr) mutable {
          DEBUGLOG("%s | send rpc request success. call method name[%s], peer "
                  "addr[%s], local addr[%s]",
                  req_protocol->msg_id_.c_str(),
                  req_protocol->method_name_.c_str(),
                  channel->getTcpClient()->getPeerAddr().address().to_string().c_str(),
                  channel->getTcpClient()->getLocalAddr().address().to_string().c_str());
        });

    DEBUGLOG("client make read message");
    channel->getTcpClient()->readMessage(
        req_protocol->msg_id_,
        [ my_controller, channel](AbstractProtocol::s_ptr msg) mutable {
          std::shared_ptr<rocket::TinyPBProtocol> rsp_protocol =
              std::dynamic_pointer_cast<rocket::TinyPBProtocol>(msg);

          DEBUGLOG("%s | success get rpc response, call method name[%s], peer "
                  "addr[%s], local addr[%s]",
                  rsp_protocol->msg_id_.c_str(),
                  rsp_protocol->method_name_.c_str(),
                  channel->getTcpClient()->getPeerAddr().address().to_string().c_str(),
                  channel->getTcpClient()->getLocalAddr().address().to_string().c_str());

          if (!(channel->getResponse()->ParseFromString(rsp_protocol->pb_data_))) {
            ERRORLOG("%s | serialize error", rsp_protocol->msg_id_.c_str());
            my_controller->SetError(ERROR_FAILED_SERIALIZE, "serialize error");
            channel->callBack();
            return;
          }

          if (rsp_protocol->err_code_ != 0) {
            ERRORLOG("%s | call rpc methood[%s] failed, error code[%d], "
                     "error info[%s]",
                     rsp_protocol->msg_id_.c_str(),
                     rsp_protocol->method_name_.c_str(),
                     rsp_protocol->err_code_, rsp_protocol->err_info_.c_str());

            my_controller->SetError(rsp_protocol->err_code_,
                                    rsp_protocol->err_info_);
            channel->callBack();
            return;
          }

          DEBUGLOG("%s | call rpc success, call method name[%s], peer addr[%s], "
                  "local addr[%s]",
                  rsp_protocol->msg_id_.c_str(),
                  rsp_protocol->method_name_.c_str(),
                  channel->getTcpClient()->getPeerAddr().address().to_string().c_str(),
                  channel->getTcpClient()->getLocalAddr().address().to_string().c_str())

          channel->callBack();
        });
  });
}

void RpcChannel::Init(controller_s_ptr controller, message_s_ptr req,
                      message_s_ptr res, closure_s_ptr done) {
  if (is_init_) {
    return;
  }
  controller_ = controller;
  request_ = req;
  response_ = res;
  closure_ = done;
  is_init_ = true;
}

google::protobuf::RpcController *RpcChannel::getController() {
  return controller_.get();
}

google::protobuf::Message *RpcChannel::getRequest() { return request_.get(); }

google::protobuf::Message *RpcChannel::getResponse() { return response_.get(); }

google::protobuf::Closure *RpcChannel::getClosure() { return closure_.get(); }

TcpClient *RpcChannel::getTcpClient() { return client_.get(); }

std::vector<tcp::endpoint> RpcChannel::FindAddr(const std::string &str) {
  size_t pos = str.find(":");
  if (pos != std::string::npos && pos < str.size() && pos > 0) {
    asio::error_code ec;
    auto addr = asio::ip::make_address(str.substr(0, pos), ec);
    auto port = std::stoi(str.substr(pos + 1));
    if (!ec && port > 0) {
      return {tcp::endpoint(addr, port)};
    }
  }

  // 根据服务名从注册中心获取
  // TODO 适配多地址
  DEBUGLOG("try to find addr in etcd registry of str[%s]", str.c_str());
  auto addrs = EtcdRegistry::GetInstance()->discoverService(str);
  if (addrs.size() > 0) {
    std::vector<tcp::endpoint> ret;
    for (const auto &addr : addrs) {
      std::string ip = addr.substr(0, addr.find(":"));
      int port = std::stoi(addr.substr(addr.find(":") + 1));
      ret.emplace_back(asio::ip::make_address(ip), port);
    }
    return ret;
  }

  // 根据服务名从配置文件中获取，调用服务地址
  // 配置当前仅支持但服务地址
  auto it = Config::GetGlobalConfig()->rpc_stubs_.find(str);
  if (it != Config::GetGlobalConfig()->rpc_stubs_.end()) {
    INFOLOG("find addr [%s] in global config of str[%s]",
            (*it).second.addr.address().to_string().c_str(), str.c_str());
    return {(*it).second.addr};
  } else {
    INFOLOG("can not find addr in global config of str[%s]", str.c_str());
    return {};
  }

  return {};
}

} // namespace rocket