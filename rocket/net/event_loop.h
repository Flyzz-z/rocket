#ifndef ROCKET_NET_EVENTPOLL_H
#define ROCKET_NET_EVENTPOLL_H

#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
namespace rocket {

class EventLoop : asio::noncopyable {
public:
  EventLoop();
  ~EventLoop();
  void run();
  void stop();
  template <typename T>
    requires std::invocable<T> &&
             std::is_same_v<std::invoke_result_t<T>, asio::awaitable<void>>
  void addCoroutine(T&& cb);

	template <typename T>
    requires std::invocable<T> &&
             std::is_same_v<std::invoke_result_t<T>, void>
  void addTimer(int interval_ms, bool isRepeat, T&& cb);
  
	asio::io_context *getIOContext();

private:
  asio::io_context m_io_context;
};


template <typename T>
  requires std::invocable<T> &&
           std::is_same_v<std::invoke_result_t<T>, asio::awaitable<void>>
void EventLoop::addCoroutine(T&& cb) {
  asio::co_spawn(m_io_context, std::forward<T>(cb), asio::detached);
}

template <typename T>
  requires std::invocable<T> && std::is_same_v<std::invoke_result_t<T>, void>
void EventLoop::addTimer(int interval_ms, bool isRepeat, T&& cb) {
  asio::co_spawn(
      m_io_context,
      [this, interval_ms, isRepeat,
       cb = std::forward<T>(cb)]() mutable -> asio::awaitable<void> {
        while (true) {
          asio::steady_timer timer(m_io_context,
                                   std::chrono::milliseconds(interval_ms));
          co_await timer.async_wait(asio::use_awaitable);
          cb();
          if (!isRepeat) {
            break;
          }
        }
      },
      asio::detached);
}
} // namespace rocket

#endif