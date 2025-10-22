#ifndef ROCKET_NET_EVENTPOLL_H
#define ROCKET_NET_EVENTPOLL_H

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <functional>

namespace rocket {

class EventLoop {
public:
  EventLoop();
  ~EventLoop();
	EventLoop(const EventLoop&) = delete;
	EventLoop& operator=(const EventLoop&) = delete; 

  void run();
  void stop();
  
  // 使用std::function替代模板约束
  void addCoroutine(std::function<asio::awaitable<void>()> cb);
  
  void addTimer(int interval_ms, bool isRepeat, std::function<void()> cb);
  
	asio::io_context *getIOContext();

	static thread_local EventLoop* t_event_loop_;

	static EventLoop* getThreadEventLoop() {
		return t_event_loop_;
	}

private:
  asio::io_context io_context_;
};



} // namespace rocket

#endif