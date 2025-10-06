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

  // TODO 包装io_context，提供统一接口
  void addTimer(int interval_ms, bool isRepeat, std::function<void()> cb);

private:
  std::unique_ptr<tcp::acceptor> m_acceptor;

  tcp::endpoint m_local_addr;

  EventLoop m_main_event_loop;

  std::unique_ptr<IOThreadGroup> m_io_thread_group; // subReactor 组

  int m_client_counts{0};

  std::set<TcpConnection::s_ptr> m_clients;
};

} // namespace rocket

#endif