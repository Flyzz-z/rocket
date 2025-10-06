#ifndef ROCKET_NET_IO_THREAD_H
#define ROCKET_NET_IO_THREAD_H

#include "rocket/net/event_loop.h"
#include <thread>
#include <semaphore>

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

 public:
  static void* Main(void* arg);


 private:
  pid_t m_thread_id {-1};    // 线程号
  std::thread m_thread;   // 线程句柄

  EventLoop m_event_loop;

  std::binary_semaphore m_init_semaphore;

  std::binary_semaphore m_start_semaphore;

};

}

#endif