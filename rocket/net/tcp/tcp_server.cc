#include "rocket/net/tcp/tcp_server.h"
#include "io_thread_group.h"
#include "rocket/net/eventloop.h"
#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/common/log.h"
#include "rocket/common/config.h"
#include <asio/co_spawn.hpp>
#include <asio/use_awaitable.hpp>
#include <memory>


namespace rocket {

TcpServer::TcpServer(tcp::endpoint local_addr) : m_local_addr(local_addr),m_main_io_context(1) {

  init(); 
  INFOLOG("rocket TcpServer listen sucess on [%s:%u]", m_local_addr.address().to_string(),m_local_addr.port());
}

TcpServer::~TcpServer() {
  m_main_io_context.stop();
  if (m_listen_fd_event) {
    delete m_listen_fd_event;
    m_listen_fd_event = NULL;
  }
}


void TcpServer::init() {

  m_acceptor = std::make_unique<tcp::acceptor>(m_main_io_context,m_local_addr);

  m_io_thread_group =	std::make_unique<IOThreadGroup>(Config::GetGlobalConfig()->m_io_threads);
  co_spawn(m_main_io_context,listener(),detached);
	
  // m_clear_client_timer_event = std::make_shared<TimerEvent>(5000, true, std::bind(&TcpServer::ClearClientTimerFunc, this));
	// m_main_event_loop->addTimerEvent(m_clear_client_timer_event);

}

/**
* listener()协程，不断 accept socket，创建新会话
* 需要在会话中提供读和写协程
* 在IO线程中使用上下文调用协程
*/
awaitable<void> TcpServer::listener() {
	for(;;) {
		auto socket = co_await m_acceptor->async_accept(use_awaitable);
		std::shared_ptr<TcpConnection> connection = std::make_shared<TcpConnection>(socket,128);
		connection->setState(Connected);
		m_client.insert(connection);
		INFOLOG("TcpServer succ get client, address=%d", socket.remote_endpoint().address());
	}
}

void TcpServer::start() {
  m_io_thread_group->start();
  m_main_io_context.run();
}


void TcpServer::ClearClientTimerFunc() {
  auto it = m_client.begin();
  for (it = m_client.begin(); it != m_client.end(); ) {
    // TcpConnection::ptr s_conn = i.second;
		// DebugLog << "state = " << s_conn->getState();
    if ((*it) != nullptr && (*it).use_count() > 0 && (*it)->getState() == Closed) {
      // need to delete TcpConnection
      DEBUGLOG("TcpConection [fd:%d] will delete, state=%d", (*it)->getFd(), (*it)->getState());
      it = m_client.erase(it);
    } else {
      it++;
    }
	
  }

}

}