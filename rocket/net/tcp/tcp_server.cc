#include "rocket/net/tcp/tcp_server.h"
#include "rocket/common/config.h"
#include "rocket/common/log.h"
#include "rocket/net/io_thread_group.h"
#include "rocket/net/tcp/tcp_connection.h"
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>
#include <memory>

namespace rocket {

TcpServer::TcpServer(tcp::endpoint local_addr)
    : m_local_addr(local_addr), m_main_io_context(1) {

  init();
  INFOLOG("rocket TcpServer listen sucess on [%s:%u]",
          m_local_addr.address().to_string().c_str(), m_local_addr.port());
}

TcpServer::~TcpServer() {
  m_main_io_context.stop();
  INFOLOG("tcp server stop");
}

void TcpServer::init() {

  m_acceptor = std::make_unique<tcp::acceptor>(m_main_io_context, m_local_addr);

  m_io_thread_group =
      std::make_unique<IOThreadGroup>(Config::GetGlobalConfig()->m_io_threads);
  co_spawn(
      m_main_io_context, [this]() -> auto { return this->listener(); },
      detached);

	addTimer(5000, true, [this]()->void{
		ClearClientTimerFunc();
	});
  // TODO 处理clean
  // m_clear_client_timer_event = std::make_shared<TimerEvent>(5000, true,
  // std::bind(&TcpServer::ClearClientTimerFunc, this));
  // m_main_event_loop->addTimerEvent(m_clear_client_timer_event);
}

/**
 * listener()协程，不断 accept socket，创建新会话
 * 需要在会话中提供读和写协程
 * 在IO线程中使用上下文调用协程
 */
awaitable<void> TcpServer::listener() {
  try {
    for (;;) {
      auto socket = co_await m_acceptor->async_accept(use_awaitable);
      auto run_io_context = m_io_thread_group->getIOThread()->getIOContext();
      INFOLOG("TcpServer succ get client, address=%s",
              socket.remote_endpoint().address().to_string().c_str());
      std::shared_ptr<TcpConnection> connection =
          std::make_shared<TcpConnection>(run_io_context, std::move(socket),
                                          128);
      connection->start();
      m_clients.insert(connection);
    }
  } catch (std::exception &e) {
    ERRORLOG("TcpServer::listener() exception: %s", e.what());
  }
}

void TcpServer::start() {
  m_io_thread_group->start();
  // asio::signal_set signals(m_main_io_context, SIGINT, SIGTERM);
  // signals.async_wait(
  //     [&io_context = m_main_io_context](auto, auto) { io_context.stop(); });
  m_main_io_context.run();
}

void TcpServer::ClearClientTimerFunc() {
  auto it = m_clients.begin();
  for (it = m_clients.begin(); it != m_clients.end();) {
    // TcpConnection::ptr s_conn = i.second;
    if ((*it) != nullptr && (*it).use_count() > 0 &&
        !((*it)->is_open())) {
      // need to delete TcpConnection
      DEBUGLOG("TcpConection will delete, because it is closed");
      it = m_clients.erase(it);
    } else {
      it++;
    }
  }
}

void TcpServer::addTimer(int interval_ms, bool isRepeat,
                         std::function<void()> cb) {
  asio::co_spawn(
      m_main_io_context,
      [this, interval_ms, isRepeat, cb]() -> awaitable<void> {

				asio::steady_timer timer(m_main_io_context);
        do {
					timer.expires_after(std::chrono::milliseconds(interval_ms));
          co_await timer.async_wait(asio::use_awaitable);
          cb();
        } while (isRepeat);
      },
      asio::detached);
}
} // namespace rocket