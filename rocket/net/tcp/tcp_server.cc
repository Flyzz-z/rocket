#include "rocket/net/tcp/tcp_server.h"
#include "rocket/net/eventloop.h"
#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/common/log.h"
#include "rocket/common/config.h"
#include <asio/co_spawn.hpp>
#include <asio/use_awaitable.hpp>
#include <memory>


namespace rocket {

TcpServer::TcpServer(std::shared_ptr<tcp::endpoint> local_addr) : m_local_addr(local_addr),m_main_io_context(1) {

  init(); 
  INFOLOG("rocket TcpServer listen sucess on [%s:%u]", m_local_addr->address().to_string(),m_local_addr->port());
}

TcpServer::~TcpServer() {
  m_main_io_context.stop();
  if (m_io_thread_group) {
    delete m_io_thread_group;
    m_io_thread_group = NULL; 
  }
  if (m_listen_fd_event) {
    delete m_listen_fd_event;
    m_listen_fd_event = NULL;
  }
}


void TcpServer::init() {

  m_acceptor = std::make_shared<tcp::acceptor>(m_main_io_context,*m_local_addr);

	//TODO: 创建io线程组,协程替换
  m_io_thread_group = new IOThreadGroup(Config::GetGlobalConfig()->m_io_threads);
  co_spawn(m_main_io_context,listener(),detached);
	
  // m_clear_client_timer_event = std::make_shared<TimerEvent>(5000, true, std::bind(&TcpServer::ClearClientTimerFunc, this));
	// m_main_event_loop->addTimerEvent(m_clear_client_timer_event);

}


awaitable<void> TcpServer::listener() {
	for(;;) {
		auto socket = co_await m_acceptor->async_accept(use_awaitable);
		std::shared_ptr<TcpConnection> connection = std::make_shared<TcpConnection>(socket,128);
		// 然后呢
	}
}

void TcpServer::onAccept() {
  auto re = m_acceptor->accept();
  int client_fd = re.first;
  NetAddr::s_ptr peer_addr = re.second;

  m_client_counts++;
  
  // 把 cleintfd 添加到任意 IO 线程里面
  IOThread* io_thread = m_io_thread_group->getIOThread();
  TcpConnection::s_ptr connetion = std::make_shared<TcpConnection>(io_thread->getEventLoop(), client_fd, 128, peer_addr, m_local_addr);
  connetion->setState(Connected);

  m_client.insert(connetion);

  INFOLOG("TcpServer succ get client, fd=%d", client_fd);
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