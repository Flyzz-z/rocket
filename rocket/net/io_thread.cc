
#include <asio/io_context.hpp>
#include <pthread.h>
#include <thread>
#include "rocket/net/io_thread.h"
#include "rocket/net/tcp/tcp_connection.h"
#include "event_loop.h"
#include "rocket/log/log.h"
#include "rocket/common/util.h"


namespace rocket {

IOThread::IOThread(): init_semaphore_(0),start_semaphore_(0) {

  thread_ = std::thread(Main, this);
  init_semaphore_.acquire();

  DEBUGLOG("IOThread [%d] create success", thread_id_);
}

IOThread::~IOThread() {
  event_loop_->stop();
  thread_.join();
}

EventLoop* IOThread::getEventLoop() {
  return event_loop_;
}

void IOThread::enqueuePendingConnection(PendingConnection pending) {
  // 加锁，将待启动的连接加入队列
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_connections_.push(std::move(pending));
  }

  // 使用原子标志位避免重复投递处理任务
  bool expected = false;
  if (processing_scheduled_.compare_exchange_strong(expected, true)) {
    // 向 io_context 投递处理任务，会在 run() 中被执行
    event_loop_->getIOContext()->post([this]() {
      processing_scheduled_.store(false);
      this->processPendingConnections();
    });
  }
}

void IOThread::processPendingConnections() {
  // 批量取出所有待启动的连接，减少锁竞争
  std::queue<PendingConnection> local_queue;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    std::swap(local_queue, pending_connections_);
  }

  // 逐个启动连接的读写协程
  while (!local_queue.empty()) {
    auto pending = std::move(local_queue.front());
    local_queue.pop();

    if (!pending.connection) {
      ERRORLOG("processPendingConnections: null connection");
      continue;
    }

    try {
      DEBUGLOG("IOThread [%d] starting TcpConnection", thread_id_);

      // 启动连接的读写协程
      pending.connection->start();

    } catch (const std::exception& e) {
      ERRORLOG("processPendingConnections failed: %s", e.what());
    }
  }
}

/*
* IOThread::Main
* 在新线程中执行io_context.run()
*/
void* IOThread::Main(void* arg) {
  IOThread* io_thread = static_cast<IOThread*> (arg);
  io_thread->thread_id_ = getThreadId();
	io_thread->event_loop_ = EventLoop::getThreadEventLoop();

  // IOThread需要长期运行，启用workGuard
  io_thread->event_loop_->enableWorkGuard();

  io_thread->init_semaphore_.release();

  DEBUGLOG("IOThread %d created, wait start semaphore", io_thread->thread_id_);

  io_thread->start_semaphore_.acquire();
  DEBUGLOG("IOThread %d start loop ", io_thread->thread_id_);

  io_thread->event_loop_->run();

  DEBUGLOG("IOThread %d end loop ", io_thread->thread_id_);

  return NULL;

}



void IOThread::start() {
  DEBUGLOG("Now invoke IOThread %d", thread_id_);
  start_semaphore_.release();
}


void IOThread::join() {
  thread_.join();
}

void IOThread::stop() {
  event_loop_->stop();
}

}
