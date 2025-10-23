#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/common/log.h"
#include "rocket/net/coder/tinypb_coder.h"
#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <memory>
#include <unistd.h>

namespace rocket {

TcpConnection::TcpConnection(asio::io_context *io_context, tcp::socket socket,
                             int buffer_size,
                             TcpConnectionType type /*= TcpConnectionByServer*/)
    : io_context_(io_context), socket_(std::move(socket)),
      timer_(*io_context), in_buffer_(buffer_size),
      out_buffer_(buffer_size), connection_type_(type) {

  local_addr_ = socket_.local_endpoint();
  peer_addr_ = socket_.remote_endpoint();
  timer_.expires_at(std::chrono::steady_clock::time_point::max());
  coder_ = new TinyPBCoder();
}

TcpConnection::~TcpConnection() {
  DEBUGLOG("~TcpConnection");
  if (coder_) {
    delete coder_;
    coder_ = NULL;
  }
}

void TcpConnection::start() {
  asio::co_spawn(
      *io_context_,
      [self = shared_from_this()]() -> awaitable<void> {
        return self->reader();
      },
      asio::detached);

  asio::co_spawn(
      *io_context_,
      [self = shared_from_this()]() -> awaitable<void> {
        return self->writer();
      },
      asio::detached);
}

/**
 * 读协程，循环读取内容，每次读取完调用execute()
 */
awaitable<void> TcpConnection::reader() {
  // 不断循环读取，每次完成读取执行excute()
  try {
    for (;;) {
      if (!socket_.is_open()) {
        ERRORLOG("onRead error, client has already disconneced, addr[%s]",
                 peer_addr_.address().to_string().c_str());
        co_return;
      }
      auto data_ptr = in_buffer_.getBuffer().prepare(in_buffer_.maxSize());
      auto bytes_read = co_await asio::async_read(
          socket_, data_ptr, asio::transfer_at_least(1), use_awaitable);
      in_buffer_.commit(bytes_read);
      execute();
    }
  } catch (std::exception &e) {
    // TODO 停止？
    ERRORLOG("onRead error, client has already disconneced, addr[%s]",
             peer_addr_.address().to_string().c_str());
  }
}

/**
 * 分 server 和 client 逻辑进行区分
 * 解码消息，进行不同处理
 */
void TcpConnection::execute() {
  if (connection_type_ == TcpConnectionByServer) {
    // 将 RPC 请求执行业务逻辑，获取 RPC 响应, 再把 RPC 响应发送回去
    std::vector<AbstractProtocol::s_ptr> result;
    coder_->decode(result, in_buffer_);
    for (size_t i = 0; i < result.size(); ++i) {
      // 1. 针对每一个请求，调用 rpc 方法，获取响应 message
      // 2. 将响应 message 放入到发送缓冲区，监听可写事件回包
      INFOLOG("success get request[%s] from client[%s]",
              result[i]->msg_id_.c_str(),
              peer_addr_.address().to_string().c_str());

      std::shared_ptr<TinyPBProtocol> message =
          std::make_shared<TinyPBProtocol>();

      RpcDispatcher::GetRpcDispatcher()->dispatch(result[i], message, this);
    }

  } else {
    // 从 buffer 里 decode 得到 message 对象, 执行其回调
    std::vector<AbstractProtocol::s_ptr> result;
    coder_->decode(result, in_buffer_);

    for (size_t i = 0; i < result.size(); ++i) {
      std::string msg_id = result[i]->msg_id_;
      auto it = read_dones_.find(msg_id);
      if (it != read_dones_.end()) {
        it->second(result[i]);
        read_dones_.erase(it);
      }
    }
  }
}

/*
 * 服务端回复客户端
 */
void TcpConnection::reply(
    std::vector<AbstractProtocol::s_ptr> &replay_messages) {
  coder_->encode(replay_messages, out_buffer_);
  listenWrite();
}

/*
 * 写协程，分客户端和服务端端逻辑进行区分
 * 客户端：
 */
awaitable<void> TcpConnection::writer() {
  try {
    while (socket_.is_open()) {

      if (out_buffer_.dataSize() > 0 || write_dones_.size() > 0) {
        // 客户端需要编码消息
        if (connection_type_ == TcpConnectionByClient) {
          // 1. 将 message encode 得到字节流
          // 2. 将字节流入到 buffer 里面，然后全部发送
          std::vector<AbstractProtocol::s_ptr> messages;

          for (size_t i = 0; i < write_dones_.size(); ++i) {
            messages.push_back(write_dones_[i].first);
          }

          coder_->encode(messages, out_buffer_);
        }

        // 错误处理
        std::size_t bytes_write = co_await asio::async_write(
            socket_, out_buffer_.getBuffer().data(), use_awaitable);
        out_buffer_.consume(bytes_write);
        INFOLOG("write bytes: %ld, to endpoint[%s]", bytes_write,
                peer_addr_.address().to_string().c_str());
        if (connection_type_ == TcpConnectionByClient) {
          for (size_t i = 0; i < write_dones_.size(); ++i) {
            write_dones_[i].second(write_dones_[i].first);
          }
          write_dones_.clear();
        }
      } else {
        asio::error_code ec;
        co_await timer_.async_wait(redirect_error(use_awaitable, ec));
      }
    }
  } catch (std::exception &e) {
    // TODO 错误处理
    ERRORLOG("TcpConnection::writer error, error info: %s", e.what());
  }
}

bool TcpConnection::is_open() {
  return socket_.is_open();
}

void TcpConnection::clear() {
  // 处理一些关闭连接后的清理动作
  if (state_ == Closed) {
    return;
  }
  state_ = Closed;
}

void TcpConnection::shutdown() {
  if (state_ == Closed || state_ == NotConnected) {
    return;
  }

  // 处于半关闭
  state_ = HalfClosing;

  // 调用 shutdown 关闭读写，意味着服务器不会再对这个 fd 进行读写操作了
  // 发送 FIN 报文， 触发了四次挥手的第一个阶段
  // 当 fd 发生可读事件，但是可读的数据为0，即 对端发送了 FIN
  // ::shutdown(fd_, SHUT_RDWR);
}

void TcpConnection::setConnectionType(TcpConnectionType type) {
  connection_type_ = type;
}

void TcpConnection::listenWrite() { timer_.cancel_one(); }

void TcpConnection::listenRead() {}

void TcpConnection::pushSendMessage(
    AbstractProtocol::s_ptr message,
    std::function<void(AbstractProtocol::s_ptr)> done) {
  write_dones_.push_back(std::make_pair(message, done));
}

void TcpConnection::pushReadMessage(
    const std::string &msg_id,
    std::function<void(AbstractProtocol::s_ptr)> done) {
  read_dones_.insert(std::make_pair(msg_id, done));
}

tcp::endpoint TcpConnection::getLocalAddr() { return local_addr_; }

tcp::endpoint TcpConnection::getPeerAddr() { return peer_addr_; }

} // namespace rocket