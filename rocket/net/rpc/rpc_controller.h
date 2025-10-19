#ifndef ROCKER_NET_RPC_RPC_CONTROLLER_H
#define ROCKER_NET_RPC_RPC_CONTROLLER_H

#include <asio/ip/tcp.hpp>
#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>
#include <string>

#include "rocket/common/log.h"

namespace rocket {

using asio::ip::tcp;

class RpcController : public google::protobuf::RpcController {

 public:
  RpcController() { INFOLOG("RpcController"); } 
  ~RpcController() { INFOLOG("~RpcController"); } 

  void Reset() override;

  bool Failed() const override;

  std::string ErrorText() const override;

  void StartCancel() override;

  void SetFailed(const std::string& reason) override;

  bool IsCanceled() const override;

  void NotifyOnCancel(google::protobuf::Closure* callback) override;

  void SetError(int32_t error_code, const std::string error_info);

  int32_t GetErrorCode();

  std::string GetErrorInfo();

  void SetMsgId(const std::string& msg_id);

  std::string GetMsgId();

  void SetLocalAddr(tcp::endpoint addr);

  void SetPeerAddr(tcp::endpoint addr);

  tcp::endpoint GetLocalAddr();

  tcp::endpoint GetPeerAddr();

  void SetTimeout(int timeout) ;
 
  int GetTimeout();

  bool Finished();

  void SetFinished(bool value);
 
 private:
  int32_t error_code_ {0};
  std::string error_info_;
  std::string msg_id_;

  bool is_failed_ {false};
  bool is_cancled_ {false};
  bool is_finished_ {false};

  tcp::endpoint local_addr_;
  tcp::endpoint peer_addr_;

  int timeout_ {1000};   // ms

};

}


#endif