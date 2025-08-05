#ifndef ROCKET_NET_IO_THREAD_H
#define ROCKET_NET_IO_THREAD_H

#include <asio/io_context.hpp>
#include <thread>
#include <semaphore>
#include "rocket/net/eventloop.h"

namespace rocket {

class IOThread {
 public:
  IOThread();
  
  ~IOThread();

  EventLoop* getEventLoop();

  void start();

  void join();

 public:
  static void* Main(void* arg);


 private:
  pid_t m_thread_id {-1};    // 线程号
  std::thread m_thread {0};   // 线程句柄

  asio::io_context m_io_context;

  std::binary_semaphore m_init_semaphore;

  std::binary_semaphore m_start_semaphore;

};

}

#endif