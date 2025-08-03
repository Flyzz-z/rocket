#ifndef ROCKET_NET_EVENTLOOP_H
#define ROCKET_NET_EVENTLOOP_H

#include <pthread.h>
#include <set>
#include <functional>
#include <queue>
#include "rocket/common/mutex.h"
#include "rocket/net/fd_event.h"
#include "rocket/net/wakeup_fd_event.h"
#include "rocket/net/timer.h"
#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>


namespace rocket {
class EventLoop {
 public:
  EventLoop();

  ~EventLoop();

  void loop();

  void wakeup();

  void stop();

  void addEvent(FdEvent* event);

  void deleteEvent(FdEvent* event);

  bool isInLoopThread();

  void addTask(std::function<void()> cb, bool is_wake_up = false);

  void addTimerEvent(TimerEvent::s_ptr event);

  bool isLooping();

 public:
  static EventLoop* GetCurrentEventLoop();


 private:
  void dealWakeup();

  void initWakeUpFdEevent();

  void initTimer();

 private:
	asio::io_context m_io_context;

  WakeUpFdEvent* m_wakeup_fd_event {NULL};

  bool m_stop_flag {false};

	std::vector<asio::ip::tcp::acceptor> m_acceptors;

  std::set<int> m_listen_fds;

  bool m_is_looping {false};
};

}


#endif