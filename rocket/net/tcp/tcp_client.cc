#include "rocket/net/tcp/tcp_client.h"
#include "event_loop.h"
#include "rocket/logger/log.h"
#include "tcp_connection.h"
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <sys/socket.h>
#include <unistd.h>

namespace rocket {

TcpClient::TcpClient(tcp::endpoint peer_addr)
    : peer_addr_(peer_addr), event_loop_(EventLoop::getThreadEventLoop()) {}

TcpClient::~TcpClient() { stop(); }

EventLoop *TcpClient::getEventLoop() { return event_loop_; }

asio::awaitable<void> TcpClient::connect() {
  auto io_context = event_loop_->getIOContext();
  try {
    tcp::socket socket(*io_context);
    co_await socket.async_connect(peer_addr_, asio::use_awaitable);
    local_addr_ = socket.local_endpoint();
    peer_addr_ = socket.remote_endpoint();
    connection_ = std::make_shared<TcpConnection>(
        io_context, std::move(socket), 128,
        TcpConnection::ConnectionType::TcpConnectionByClient);
    connection_->start();
  } catch (std::exception &e) {
    INFOLOG("tcp connect error %s", e.what());
    connect_error_info_ = std::string("tcp connect exception: ") + e.what();
  }
}

void TcpClient::stop() {
  if (connection_) {
    connection_->shutdown();
  }
}

// 异步的发送 message
// 如果发送 message 成功，会调用 done 函数， 函数的入参就是 message 对象
void TcpClient::writeMessage(
    AbstractProtocol::s_ptr message,
    std::function<void(AbstractProtocol::s_ptr)> done) {
  // 1. 把 message 对象写入到 Connection 的 buffer, done 也写入
  // 2. 启动 connection 可写事件
  if (connection_) {
    connection_->pushSendMessage(message, done);
    connection_->listenWrite();
  }
}

// 异步的读取 message
// 如果读取 message 成功，会调用 done 函数， 函数的入参就是 message 对象
void TcpClient::readMessage(const std::string &msg_id,
                            std::function<void(AbstractProtocol::s_ptr)> done) {
  // 1. 监听可读事件
  // 2. 从 buffer 里 decode 得到 message 对象, 判断是否 msg_id
  // 相等，相等则读成功，执行其回调
  if (connection_) {
    connection_->pushReadMessage(msg_id, done);
    connection_->listenRead();
  }
}

int TcpClient::getConnectErrorCode() { return connect_error_code_; }

std::string TcpClient::getConnectErrorInfo() { return connect_error_info_; }

tcp::endpoint TcpClient::getPeerAddr() { return peer_addr_; }

tcp::endpoint TcpClient::getLocalAddr() { return local_addr_; }
} // namespace rocket