#include "rocket/net/event_loop.h"
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>

namespace rocket {

thread_local std::unique_ptr<EventLoop> EventLoop::t_event_loop_ = nullptr;

EventLoop::EventLoop() {
  // 默认不创建work_guard
}

EventLoop *EventLoop::getThreadEventLoop() {
  if (t_event_loop_ == nullptr) {
    t_event_loop_ = std::make_unique<EventLoop>();
  }
  return t_event_loop_.get();
}

EventLoop::~EventLoop() { 
  disableWorkGuard();
}

void EventLoop::run() { 
  io_context_.run(); 
}

void EventLoop::stop() { 
  work_guard_.reset();
  io_context_.stop(); 
}

asio::io_context *EventLoop::getIOContext() { return &io_context_; }

void EventLoop::enableWorkGuard() {
  if (!work_guard_) {
    work_guard_ = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(
        asio::make_work_guard(io_context_));
  }
}

void EventLoop::disableWorkGuard() {
  work_guard_.reset();
}

void EventLoop::addCoroutine(std::function<asio::awaitable<void>()> cb) {
  asio::co_spawn(io_context_, std::move(cb), asio::detached);
}

void EventLoop::addTimer(int interval_ms, bool isRepeat,
                         std::function<void()> cb) {

  asio::co_spawn(
      io_context_,
      [this, interval_ms, isRepeat,
       cb = std::move(cb)]() mutable -> asio::awaitable<void> {
        while (true) {
          asio::steady_timer timer(io_context_,
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