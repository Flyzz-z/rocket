#ifndef ROCKET_NET_RPC_RPC_CLOSURE_H
#define ROCKET_NET_RPC_RPC_CLOSURE_H

#include <google/protobuf/stubs/callback.h>
#include <functional>
#include <memory>
#include "rocket/common/run_time.h"
#include "rocket/logger/log.h"
#include "rocket/common/exception.h"
#include "rocket/net/rpc/rpc_interface.h"

namespace rocket {

class RpcClosure : public google::protobuf::Closure {
 public:
  typedef std::shared_ptr<RpcInterface> it_s_ptr;

  RpcClosure(it_s_ptr interface, std::function<void()> cb) : rpc_interface_(interface), cb_(cb) {
    DEBUGLOG("RpcClosure");
  }

  ~RpcClosure() {
    DEBUGLOG("~RpcClosure");
  }

  void Run() override {

    // 更新 runtime 的 RpcInterFace, 这里在执行 cb 的时候，都会以 RpcInterface 找到对应的接口，实现打印 app 日志等
    if (!rpc_interface_) {
      RunTime::GetRunTime()->rpc_interface_ = rpc_interface_.get();
    }

    try {
      if (cb_ != nullptr) {
        cb_();
      }
      if (rpc_interface_) {
        rpc_interface_.reset();
      }
    } catch (RocketException& e) {
      ERRORLOG("RocketException exception[%s], deal handle", e.what());
      e.handle();
      if (rpc_interface_) {
        rpc_interface_->setError(e.errorCode(), e.errorInfo());
        rpc_interface_.reset();
      }
    } catch (std::exception& e) {
      ERRORLOG("std::exception[%s]", e.what());
      if (rpc_interface_) {
        rpc_interface_->setError(-1, "unkonwn std::exception");
        rpc_interface_.reset();
      }
    } catch (...) {
      ERRORLOG("Unkonwn exception");
      if (rpc_interface_) {
        rpc_interface_->setError(-1, "unkonwn exception");
        rpc_interface_.reset();
      }
    }
    
  }

 private:
  it_s_ptr rpc_interface_ {nullptr};
  std::function<void()> cb_ {nullptr};

};

}
#endif