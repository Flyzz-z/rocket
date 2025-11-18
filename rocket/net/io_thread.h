#ifndef ROCKET_NET_IO_THREAD_H
#define ROCKET_NET_IO_THREAD_H

#include "rocket/net/event_loop.h"
#include "rocket/net/pending_connection.h"
#include <thread>
#include <semaphore>
#include <queue>
#include <mutex>
#include <atomic>

namespace rocket {

/**
* 处理IO的线程，连接上下文提供的读写协程在IO线程中处理
*/
class IOThread {
 public:
  IOThread();

  ~IOThread();

  EventLoop* getEventLoop();

  void start();

  void join();

	void stop();

  // 将待启动的 TcpConnection 加入队列
  void enqueuePendingConnection(PendingConnection pending);

 public:
  static void* Main(void* arg);

 private:
  // 处理待启动队列中的 TcpConnection，调用 start() 启动协程
  void processPendingConnections();

 private:
  pid_t thread_id_ {-1};    // 线程号
  std::thread thread_;   // 线程句柄

  EventLoop *event_loop_;

  std::binary_semaphore init_semaphore_;

  std::binary_semaphore start_semaphore_;

  // 待启动的连接队列
  std::mutex pending_mutex_;
  std::queue<PendingConnection> pending_connections_;

  // 标志位：是否已经投递了处理任务（避免重复投递）
  std::atomic<bool> processing_scheduled_{false};

};

}

#endif