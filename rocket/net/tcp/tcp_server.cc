#include "rocket/net/tcp/tcp_server.h"
#include "event_loop.h"
#include "rocket/common/config.h"
#include "rocket/logger/log.h"
#include "rocket/net/io_thread_group.h"
#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/net/pending_connection.h"
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
 * accept 线程负责：accept() + 创建 TcpConnection 对象
 * IO 线程负责：调用 connection->start() 启动读写协程
 */
awaitable<void> TcpServer::listener() {
  for (;;) {
    asio::error_code ec;
    auto socket = co_await acceptor_->async_accept(redirect_error(use_awaitable, ec));
    if (ec) {
      ERRORLOG("TcpServer::listener() error: %s", ec.message().c_str());
      co_return;
    }

    // Round-robin 选择一个 IO 线程
    IOThread* io_thread = io_thread_group_->getIOThread();
    EventLoop* run_event_loop = io_thread->getEventLoop();
    auto run_io_context = run_event_loop->getIOContext();

    DEBUGLOG("TcpServer succ get client, address=%s",
            socket.remote_endpoint().address().to_string().c_str());

    // 在 accept 线程中创建 TcpConnection 对象（轻量级操作）
    std::shared_ptr<TcpConnection> connection =
        std::make_shared<TcpConnection>(run_io_context, std::move(socket), 128);


    clients_.insert(connection);

    // 将待启动的连接投递到 IO 线程队列，由 IO 线程调用 start()（重量级操作）
    io_thread->enqueuePendingConnection(PendingConnection(connection));
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