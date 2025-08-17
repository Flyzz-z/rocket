#ifndef ROCKET_NET_TCP_SERVER_H
#define ROCKET_NET_TCP_SERVER_H

#include <asio/ip/tcp.hpp>
#include <memory>
#include <set>
#include "rocket/net/tcp/tcp_acceptor.h"
#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/net/tcp/net_addr.h"
#include "rocket/net/io_thread_group.h"
#include <asio/use_awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>


namespace rocket {

using asio::ip::tcp;
using asio::awaitable;
using asio::co_spawn;
using asio::detached;
using asio::use_awaitable;

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
	std::unique_ptr<tcp::acceptor> m_acceptor;

	tcp::endpoint m_local_addr;
	
  asio::io_context m_main_io_context;
  
  std::unique_ptr<IOThreadGroup> m_io_thread_group;   // subReactor 组

  int m_client_counts {0};

  std::set<TcpConnection::s_ptr> m_client;


};

}


#endif