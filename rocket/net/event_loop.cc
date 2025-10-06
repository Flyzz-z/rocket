#include "rocket/net/event_loop.h"
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/steady_timer.hpp>

namespace rocket {

EventLoop::EventLoop() : m_io_context(1) {}

EventLoop::~EventLoop() { stop(); }

void EventLoop::run() { m_io_context.run(); }

void EventLoop::stop() { m_io_context.stop(); }

asio::io_context *EventLoop::getIOContext() { return &m_io_context; }

} // namespace rocket