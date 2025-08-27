#ifndef ROCKET_NET_TCP_TCP_CLIENT_H
#define ROCKET_NET_TCP_TCP_CLIENT_H

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <memory>
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

  // 异步的进行 conenct
  // 如果 connect 完成，done 会被执行
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

  //void addTimerEvent(TimerEvent::s_ptr timer_event);
	void addTimer(int interval_ms, bool isRepeat ,std::function<void()> cb);

	void addCoFunc(std::function<asio::awaitable<void>()> cb);


 private:
  tcp::endpoint m_peer_addr;	
  tcp::endpoint m_local_addr;

	IOThreadSingleton *m_io_thread_singleton;
	asio::io_context *m_io_context;

  TcpConnection::s_ptr m_connection;

  int m_connect_error_code {0};
  std::string m_connect_error_info;

};  
}

#endif