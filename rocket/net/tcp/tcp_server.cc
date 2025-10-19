#include "rocket/net/tcp/tcp_server.h"
#include "event_loop.h"
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
    : local_addr_(local_addr){

  init();
  INFOLOG("rocket TcpServer listen sucess on [%s:%u]",
          local_addr_.address().to_string().c_str(), local_addr_.port());
}

TcpServer::~TcpServer() {
  main_event_loop_.stop();
  INFOLOG("tcp server stop");
}

void TcpServer::init() {

	auto main_io_context = main_event_loop_.getIOContext();
  acceptor_ = std::make_unique<tcp::acceptor>(*main_io_context, local_addr_);

  io_thread_group_ =
      std::make_unique<IOThreadGroup>(Config::GetGlobalConfig()->io_threads_);
	main_event_loop_.addCoroutine([this]() -> auto { return this->listener(); });
	main_event_loop_.addTimer(5000, true, [this]()->void{
		ClearClientTimerFunc();
	});
}

/**
 * listener()协程，不断 accept socket，创建新会话
 * 需要在会话中提供读和写协程
 * 在IO线程中使用上下文调用协程
 */
awaitable<void> TcpServer::listener() {
  try {
    for (;;) {
      auto socket = co_await acceptor_->async_accept(use_awaitable);
			EventLoop* run_event_loop = io_thread_group_->getIOThread()->getEventLoop();
      auto run_io_context = run_event_loop->getIOContext();
      INFOLOG("TcpServer succ get client, address=%s",
              socket.remote_endpoint().address().to_string().c_str());
      std::shared_ptr<TcpConnection> connection =
          std::make_shared<TcpConnection>(run_io_context, std::move(socket),
                                          128);
      connection->start();
      clients_.insert(connection);
    }
  } catch (std::exception &e) {
    ERRORLOG("TcpServer::listener() exception: %s", e.what());
  }
}

void TcpServer::start() {
  io_thread_group_->start();
  // asio::signal_set signals(main_io_context_, SIGINT, SIGTERM);
  // signals.async_wait(
  //     [&io_context = main_io_context_](auto, auto) { io_context.stop(); });
  main_event_loop_.run();
}

void TcpServer::ClearClientTimerFunc() {
  auto it = clients_.begin();
  for (it = clients_.begin(); it != clients_.end();) {
    // TcpConnection::ptr s_conn = i.second;
    if ((*it) != nullptr && (*it).use_count() > 0 &&
        !((*it)->is_open())) {
      // need to delete TcpConnection
      DEBUGLOG("TcpConection will delete, because it is closed");
      it = clients_.erase(it);
    } else {
      it++;
    }
  }
}
} // namespace rocket