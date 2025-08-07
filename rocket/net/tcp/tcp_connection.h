#ifndef ROCKET_NET_TCP_TCP_CONNECTION_H
#define ROCKET_NET_TCP_TCP_CONNECTION_H

#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_awaitable.hpp>
#include <memory>
#include <map>
#include <queue>
#include "rocket/net/tcp/net_addr.h"
#include "rocket/net/tcp/tcp_buffer.h"
#include "rocket/net/io_thread.h"
#include "rocket/net/coder/abstract_coder.h"
#include "rocket/net/rpc/rpc_dispatcher.h"

namespace rocket {

using asio::ip::tcp;
using asio::awaitable;
using asio::use_awaitable;
using TcpDataBuffer = asio::dynamic_vector_buffer<char, std::allocator<char>>;

enum TcpState {
  NotConnected = 1,
  Connected = 2,
  HalfClosing = 3,
  Closed = 4,
};

enum TcpConnectionType {
  TcpConnectionByServer = 1,  // 作为服务端使用，代表跟对端客户端的连接
  TcpConnectionByClient = 2,  // 作为客户端使用，代表跟对赌服务端的连接
};

class TcpConnection {
 public:

  typedef std::shared_ptr<TcpConnection> s_ptr;


 public:
  TcpConnection(asio::io_context *io_context,tcp::socket socket, int buffer_size, TcpConnectionType type = TcpConnectionByServer);

  ~TcpConnection();

  awaitable<void> reader();

  void execute();

  awaitable<void> writer();

  void setState(const TcpState state);

  TcpState getState();

  void clear();

  int getFd();

  // 服务器主动关闭连接
  void shutdown();

  void setConnectionType(TcpConnectionType type);

  // 启动监听可写事件
  void listenWrite();

  // 启动监听可读事件
  void listenRead();

  void pushSendMessage(AbstractProtocol::s_ptr message, std::function<void(AbstractProtocol::s_ptr)> done);

  void pushReadMessage(const std::string& msg_id, std::function<void(AbstractProtocol::s_ptr)> done);

  tcp::endpoint getLocalAddr();

  tcp::endpoint getPeerAddr();

  void reply(std::vector<AbstractProtocol::s_ptr>& replay_messages);

 private:

 	asio::io_context *m_io_context;

  tcp::socket m_socket;

  tcp::endpoint m_local_addr;
  tcp::endpoint m_peer_addr;

  TcpDataBuffer m_in_buffer;
  TcpDataBuffer m_out_buffer;
  int m_buffer_size;
	std::vector<char> m_in_vector;
	std::vector<char> m_out_vector; 

  FdEvent* m_fd_event {NULL};

  AbstractCoder* m_coder {NULL};

  TcpState m_state;

  int m_fd {0};

  TcpConnectionType m_connection_type {TcpConnectionByServer};

  // std::pair<AbstractProtocol::s_ptr, std::function<void(AbstractProtocol::s_ptr)>>
  std::vector<std::pair<AbstractProtocol::s_ptr, std::function<void(AbstractProtocol::s_ptr)>>> m_write_dones;

  // key 为 msg_id
  std::map<std::string, std::function<void(AbstractProtocol::s_ptr)>> m_read_dones;
  
};

}

#endif
