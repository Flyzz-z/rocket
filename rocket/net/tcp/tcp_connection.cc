#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/logger/log.h"
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
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace rocket {

TcpConnection::TcpConnection(asio::io_context *io_context, tcp::socket socket,
                             int buffer_size,
                             ConnectionType type /*= TcpConnectionByServer*/)
    : io_context_(io_context), socket_(std::move(socket)), timer_(*io_context),
      in_buffer_(buffer_size), out_buffer_(buffer_size),
      connection_type_(type) {

  local_addr_ = socket_.local_endpoint();
  peer_addr_ = socket_.remote_endpoint();
  timer_.expires_at(std::chrono::steady_clock::time_point::max());
  coder_ = std::make_unique<TinyPBCoder>();
}

TcpConnection::~TcpConnection() {
  DEBUGLOG("~TcpConnection");
  // 确保socket被正确关闭
  shutdown();
}

void TcpConnection::start() {

  state_.store(State::Connected, std::memory_order_relaxed);
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

  for (;;) {
    if (!is_open()) {
      ERRORLOG("onRead error, client has already disconneced, addr[%s]",
               peer_addr_.address().to_string().c_str());
      co_return;
    }
    auto data_ptr = in_buffer_.getBuffer().prepare(in_buffer_.maxSize());
    asio::error_code ec;
    auto bytes_read =
        co_await asio::async_read(socket_, data_ptr, asio::transfer_at_least(1),
                                  redirect_error(use_awaitable, ec));
    if (ec) {
      if (ec == asio::error::operation_aborted) {
        // 操作被取消，通常是主动关闭连接
        DEBUGLOG("async_read was cancelled, connection closing, addr[%s]",
                 peer_addr_.address().to_string().c_str());
      } else {
        // 其他错误，通常是连接被对端关闭
        INFOLOG("async_read error, error info: %s, addr[%s]",
                 ec.message().c_str(),
                 peer_addr_.address().to_string().c_str());
        shutdown();
      }
      co_return;
    }
    in_buffer_.commit(bytes_read);
    execute();
  }
}

/**
 * 分 server 和 client 逻辑进行区分
 * 解码消息，进行不同处理
 */
void TcpConnection::execute() {
  if (connection_type_ == ConnectionType::TcpConnectionByServer) {
    // 将 RPC 请求执行业务逻辑，获取 RPC 响应, 再把 RPC 响应发送回去
    std::vector<AbstractProtocol::s_ptr> result;
    coder_->decode(result, in_buffer_);
    for (size_t i = 0; i < result.size(); ++i) {
      // 1. 针对每一个请求，调用 rpc 方法，获取响应 message
      // 2. 将响应 message 放入到发送缓冲区，监听可写事件回包
      DEBUGLOG("success get request[%s] from client[%s]",
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

  while (is_open()) {

    if (out_buffer_.dataSize() > 0 || write_dones_.size() > 0) {
      // 客户端需要编码消息
      if (connection_type_ == ConnectionType::TcpConnectionByClient) {
        // 1. 将 message encode 得到字节流
        // 2. 将字节流入到 buffer 里面，然后全部发送
        std::vector<AbstractProtocol::s_ptr> messages;

        for (size_t i = 0; i < write_dones_.size(); ++i) {
          messages.push_back(write_dones_[i].first);
        }

        coder_->encode(messages, out_buffer_);
      }

      // 错误处理
      asio::error_code ec;
      std::size_t bytes_write =
          co_await asio::async_write(socket_, out_buffer_.getBuffer().data(),
                                     redirect_error(use_awaitable, ec));
      if (ec) {
        if (ec == asio::error::operation_aborted) {
          // 操作被取消，通常是主动关闭连接
          DEBUGLOG("async_write was cancelled, connection closing, addr[%s]",
                   peer_addr_.address().to_string().c_str());
        } else {
          // 其他错误，通常是连接被对端关闭
          INFOLOG("async_write error, error info: %s, addr[%s]",
                   ec.message().c_str(),
                   peer_addr_.address().to_string().c_str());
          shutdown();
        }
        co_return;
      }

      out_buffer_.consume(bytes_write);
      DEBUGLOG("write bytes: %ld, to endpoint[%s]", bytes_write,
               peer_addr_.address().to_string().c_str());
      if (connection_type_ == ConnectionType::TcpConnectionByClient) {
        for (size_t i = 0; i < write_dones_.size(); ++i) {
          write_dones_[i].second(write_dones_[i].first);
        }
        write_dones_.clear();
      }
    } else {
      asio::error_code ec;
      co_await timer_.async_wait(redirect_error(use_awaitable, ec));
      // 不需要特别处理timer等待的错误，因为这通常是正常的通知机制
    }
  }
}

bool TcpConnection::is_open() {
  return state_.load(std::memory_order_relaxed) == State::Connected && socket_.is_open();
}

void TcpConnection::clear() {
  // 关闭socket
  asio::error_code ec;

  // 清空回调函数列表
  write_dones_.clear();
  read_dones_.clear();

  // 更新连接状态
  state_.store(State::Closed, std::memory_order_relaxed);
}

// todo 当前不支持shutdown，只支持close
void TcpConnection::shutdown() {
  if (state_.load(std::memory_order_relaxed) == State::Closed) {
    return;
  }

  // 更新状态
  state_.store(State::Closed, std::memory_order_relaxed);

  socket_.cancel();
  // 取消定时器
  timer_.cancel();
  // 关闭socket
  socket_.close();

  // 清空缓冲区和回调列表
  write_dones_.clear();
  read_dones_.clear();
}

void TcpConnection::setConnectionType(ConnectionType type) {
  connection_type_ = type;
}

void TcpConnection::listenWrite() { timer_.cancel(); }

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