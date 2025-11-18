#ifndef ROCKET_NET_TCP_TCP_CONNECTION_H
#define ROCKET_NET_TCP_TCP_CONNECTION_H

#include "rocket/net/coder/abstract_coder.h"
#include "rocket/net/rpc/rpc_dispatcher.h"
#include "tcp_buffer.h"
#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/redirect_error.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <map>
#include <memory>

namespace rocket {

using asio::awaitable;
using asio::redirect_error;
using asio::use_awaitable;
using asio::ip::tcp;

class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
  typedef std::shared_ptr<TcpConnection> s_ptr;

  enum class State {
    NotConnected = 1,
    Connected = 2,
    HalfClosing = 3,
    Closed = 4,
  };

  enum class ConnectionType {
    TcpConnectionByServer = 1, // 作为服务端使用，代表跟对端客户端的连接
    TcpConnectionByClient = 2, // 作为客户端使用，代表跟对赌服务端的连接
  };

public:
  TcpConnection(asio::io_context *io_context, tcp::socket socket,
                int buffer_size,
                ConnectionType type =
                    TcpConnection::ConnectionType::TcpConnectionByServer);

  ~TcpConnection();

	TcpConnection(const TcpConnection&) = delete;
	TcpConnection& operator=(const TcpConnection&) = delete;

	TcpConnection(TcpConnection&&);

  void start();

  void execute();

  bool is_open();

  void clear();

  // 服务器主动关闭连接
  void shutdown();

  void setConnectionType(ConnectionType type);

  // 启动监听可写事件
  void listenWrite();

  // 启动监听可读事件
  void listenRead();

  void pushSendMessage(AbstractProtocol::s_ptr message,
                       std::function<void(AbstractProtocol::s_ptr)> done);

  void pushReadMessage(const std::string &msg_id,
                       std::function<void(AbstractProtocol::s_ptr)> done);

  tcp::endpoint getLocalAddr();

  tcp::endpoint getPeerAddr();

  void reply(std::vector<AbstractProtocol::s_ptr> &replay_messages);

private:
  awaitable<void> reader();
  awaitable<void> writer();

  asio::io_context *io_context_;

  tcp::socket socket_;

  tcp::endpoint local_addr_;
  tcp::endpoint peer_addr_;

  asio::steady_timer timer_;

  TcpBuffer in_buffer_;
  TcpBuffer out_buffer_;

  std::unique_ptr<AbstractCoder> coder_{nullptr};

  State state_{State::NotConnected};

  ConnectionType connection_type_{ConnectionType::TcpConnectionByServer};

  // std::pair<AbstractProtocol::s_ptr,
  // std::function<void(AbstractProtocol::s_ptr)>>
  std::vector<std::pair<AbstractProtocol::s_ptr,
                        std::function<void(AbstractProtocol::s_ptr)>>>
      write_dones_;

  // key 为 msg_id
  std::map<std::string, std::function<void(AbstractProtocol::s_ptr)>>
      read_dones_;
};

} // namespace rocket

#endif
