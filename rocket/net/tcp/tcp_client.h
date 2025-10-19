#ifndef ROCKET_NET_TCP_TCP_CLIENT_H
#define ROCKET_NET_TCP_TCP_CLIENT_H

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <memory>
#include "event_loop.h"
#include "rocket/net/io_thread_singleton.h"
#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/net/coder/abstract_protocol.h"


namespace rocket {

using asio::ip::tcp;

class TcpClient {
 public:
  typedef std::shared_ptr<TcpClient> s_ptr;

  TcpClient(tcp::endpoint peer_addr);

  ~TcpClient();

	EventLoop* getEventLoop();

  asio::awaitable<void> connect();

  // 异步的发送 message
  // 如果发送 message 成功，会调用 done 函数， 函数的入参就是 message 对象 
  void writeMessage(AbstractProtocol::s_ptr message, std::function<void(AbstractProtocol::s_ptr)> done);


  // 异步的读取 message
  // 如果读取 message 成功，会调用 done 函数， 函数的入参就是 message 对象 
  void readMessage(const std::string& msg_id, std::function<void(AbstractProtocol::s_ptr)> done);

  void stop();

  int getConnectErrorCode();

  std::string getConnectErrorInfo();

  tcp::endpoint getPeerAddr();

  tcp::endpoint getLocalAddr();

 private:
  tcp::endpoint peer_addr_;	
  tcp::endpoint local_addr_;

	IOThreadSingleton *io_thread_singleton_;
	EventLoop* event_loop_;

  TcpConnection::s_ptr connection_;

  int connect_error_code_ {0};
  std::string connect_error_info_;

};  
}

#endif