#include "rocket/net/event_loop.h"
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>
#include <iostream>

namespace rocket {

thread_local EventLoop* EventLoop::t_event_loop_;

EventLoop::EventLoop() { 
	t_event_loop_ = this;
}

EventLoop::~EventLoop() { stop(); }

void EventLoop::run() { io_context_.run(); }

void EventLoop::stop() { io_context_.stop(); }

asio::io_context *EventLoop::getIOContext() { return &io_context_; }

void EventLoop::addCoroutine(std::function<asio::awaitable<void>()> cb) {
  asio::co_spawn(io_context_, std::move(cb), asio::detached);
}

void EventLoop::addTimer(int interval_ms, bool isRepeat, std::function<void()> cb) {

  asio::co_spawn(
      io_context_,
      [this, interval_ms, isRepeat, cb = std::move(cb)]() mutable -> asio::awaitable<void> {
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