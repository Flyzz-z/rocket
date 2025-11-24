#include "rocket/logger/log.h"
#include "rocket/net/rpc/rpc_closure.h"
#include "rocket/net/rpc/rpc_closure.h"
#include "rocket/net/rpc/rpc_controller.h"
#include "rocket/net/rpc/rpc_interface.h"

namespace rocket {

RpcInterface::RpcInterface(const google::protobuf::Message* req, google::protobuf::Message* rsp, RpcClosure* done, RpcController* controller)
  : req_base_(req), rsp_base_(rsp), done_(done) , controller_(controller) {
    DEBUGLOG("RpcInterface");
}

RpcInterface::~RpcInterface() {
  DEBUGLOG("~RpcInterface");

  reply();

  destroy();

}

void RpcInterface::reply() {
  // reply to client
  // you should call is when you wan to set response back
  // it means this rpc method done 
  if (done_) {
    done_->Run();
  }

}

std::shared_ptr<RpcClosure> RpcInterface::newRpcClosure(std::function<void()>& cb) {
  return std::make_shared<RpcClosure>(shared_from_this(), cb);
}


void RpcInterface::destroy() {
  if (req_base_) {
    delete req_base_;
    req_base_ = NULL;
  }

  if (rsp_base_) {
    delete rsp_base_;
    rsp_base_ = NULL;
  }

  if (done_) {
    delete done_;
    done_ = NULL;
  }

  if (controller_) {
    delete controller_;
    controller_ = NULL;
  }

}


}