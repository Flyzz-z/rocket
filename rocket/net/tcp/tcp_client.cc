#include <asio/awaitable.hpp>
#include <sys/socket.h>
#include <unistd.h>
#include "rocket/common/log.h"
#include "rocket/net/tcp/tcp_client.h"
#include "rocket/common/error_code.h"
#include "tcp_connection.h"

//TODO 完善client
namespace rocket {

TcpClient::TcpClient(tcp::endpoint peer_addr) : m_peer_addr(peer_addr) {
	m_io_thread_singleton = IOThreadSingleton::GetInstance();
	m_io_thread_singleton->start();
	m_io_context = m_io_thread_singleton->getIOContext();
	//m_connection = std::make_shared<TcpConnection>(m_io_context, tcp::socket(*m_io_context,peer_addr),128, TcpConnectionByClient);
}

TcpClient::~TcpClient() {}

// 异步的进行 conenct
// 如果connect 成功，done 会被执行
asio::awaitable<void> TcpClient::connect() {
	
	try {
		tcp::socket socket(*m_io_context);
		co_await socket.async_connect(m_peer_addr, asio::use_awaitable);
		m_local_addr = socket.local_endpoint();
		m_peer_addr = socket.remote_endpoint();
		m_connection = std::make_shared<TcpConnection>(m_io_context, std::move(socket),128, TcpConnectionByClient);
		m_connection->start();
	} catch (std::exception& e) { 
		INFOLOG("tcp connect error %s",e.what());
		// m_connect_error_code = ErrorCode::TCP_CONNECT_ERROR;
		m_connect_error_info = std::string("tcp connect exception: ") + e.what();
	}
}


void TcpClient::stop() {
	m_io_thread_singleton->stop();
}

// 异步的发送 message
// 如果发送 message 成功，会调用 done 函数， 函数的入参就是 message 对象 
void TcpClient::writeMessage(AbstractProtocol::s_ptr message, std::function<void(AbstractProtocol::s_ptr)> done) {
  // 1. 把 message 对象写入到 Connection 的 buffer, done 也写入
  // 2. 启动 connection 可写事件
  m_connection->pushSendMessage(message, done);
  m_connection->listenWrite();

}


// 异步的读取 message
// 如果读取 message 成功，会调用 done 函数， 函数的入参就是 message 对象 
void TcpClient::readMessage(const std::string& msg_id, std::function<void(AbstractProtocol::s_ptr)> done) {
  // 1. 监听可读事件
  // 2. 从 buffer 里 decode 得到 message 对象, 判断是否 msg_id 相等，相等则读成功，执行其回调
  m_connection->pushReadMessage(msg_id, done);
  m_connection->listenRead();
}

int TcpClient::getConnectErrorCode() {
  return m_connect_error_code;
}

std::string TcpClient::getConnectErrorInfo() {
  return m_connect_error_info;

}

tcp::endpoint TcpClient::getPeerAddr() {
  return m_peer_addr;
}

tcp::endpoint TcpClient::getLocalAddr() {
  return m_local_addr;
}


// void TcpClient::addTimerEvent(TimerEvent::s_ptr timer_event) {
//   m_event_loop->addTimerEvent(timer_event);
// }

}