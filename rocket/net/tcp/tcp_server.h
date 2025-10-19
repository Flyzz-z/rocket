#ifndef ROCKET_NET_TCP_SERVER_H
#define ROCKET_NET_TCP_SERVER_H

#include "event_loop.h"
#include "rocket/net/io_thread_group.h"
#include "rocket/net/tcp/tcp_connection.h"
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/use_awaitable.hpp>
#include <etcd/Value.hpp>
#include <memory>
#include <set>

namespace rocket {

using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;
using asio::ip::tcp;

class TcpServer {
public:
  TcpServer(tcp::endpoint local_addr);

  ~TcpServer();

  void start();

private:
  void init();

  // 当有新客户端连接之后需要执行
  awaitable<void> listener();

  // 清除 closed 的连接
  void ClearClientTimerFunc();

private:
  std::unique_ptr<tcp::acceptor> acceptor_;

  tcp::endpoint local_addr_;

  EventLoop main_event_loop_;

  std::unique_ptr<IOThreadGroup> io_thread_group_; // subReactor 组

  int client_counts_{0};

  std::set<TcpConnection::s_ptr> clients_;
};

} // namespace rocket

#endif