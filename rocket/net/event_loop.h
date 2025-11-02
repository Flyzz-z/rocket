#ifndef ROCKET_NET_EVENTPOLL_H
#define ROCKET_NET_EVENTPOLL_H

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <functional>
#include <memory>

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
  
  // 为IOThread等需要长期运行的场景提供work guard支持
  void enableWorkGuard();
  void disableWorkGuard();

	static thread_local std::unique_ptr<EventLoop> t_event_loop_;

	static EventLoop* getThreadEventLoop();

private:
  asio::io_context io_context_;
  // 为需要长期运行的场景提供work_guard支持
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_;
};



} // namespace rocket

#endif