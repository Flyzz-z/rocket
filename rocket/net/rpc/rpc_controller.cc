
#include "rocket/net/rpc/rpc_controller.h"

namespace rocket {

void RpcController::Reset() {
  error_code_ = 0;
  error_info_ = "";
  msg_id_ = "";
  is_failed_ = false;
  is_cancled_ = false;
  is_finished_ = false;
  timeout_ = 1000;   // ms
}

bool RpcController::Failed() const {
  return is_failed_;
}

std::string RpcController::ErrorText() const {
  return error_info_;
}

void RpcController::StartCancel() {
  is_cancled_ = true;
  is_failed_ = true;
  SetFinished(true);
}

void RpcController::SetFailed(const std::string& reason) {
  error_info_ = reason;
  is_failed_ = true;
}

bool RpcController::IsCanceled() const {
  return is_cancled_;
}

void RpcController::NotifyOnCancel(google::protobuf::Closure* callback) {

}


void RpcController::SetError(int32_t error_code, const std::string error_info) {
  error_code_ = error_code;
  error_info_ = error_info;
  is_failed_ = true;
}

int32_t RpcController::GetErrorCode() {
  return error_code_;
}

std::string RpcController::GetErrorInfo() {
  return error_info_;
}

void RpcController::SetMsgId(const std::string& msg_id) {
  msg_id_ = msg_id;
}

std::string RpcController::GetMsgId() {
  return msg_id_;
}

void RpcController::SetLocalAddr(tcp::endpoint addr) {
  local_addr_ = addr;
}

void RpcController::SetPeerAddr(tcp::endpoint addr) {
  peer_addr_ = addr;
}

tcp::endpoint RpcController::GetLocalAddr() {
  return local_addr_;
}

tcp::endpoint RpcController::GetPeerAddr() {
  return peer_addr_;
}

void RpcController::SetTimeout(int timeout) {
  timeout_ = timeout;
}

int RpcController::GetTimeout() {
  return timeout_;
}

bool RpcController::Finished() {
  return is_finished_;
}

void RpcController::SetFinished(bool value) {
  is_finished_ = value;
}

}