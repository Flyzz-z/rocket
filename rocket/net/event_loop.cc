#include "rocket/net/event_loop.h"
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>

namespace rocket {

EventLoop::EventLoop() : io_context_(1) {}

EventLoop::~EventLoop() { stop(); }

void EventLoop::run() { io_context_.run(); }

void EventLoop::stop() { io_context_.stop(); }

asio::io_context *EventLoop::getIOContext() { return &io_context_; }

} // namespace rocket