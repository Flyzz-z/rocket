#include "rocket/common/thread_local_buffer.h"
#include "rocket/common/log.h"
#include "util.h"
#include <atomic>
#include <mutex>

namespace rocket {

ThreadLocalLogBuffer::ThreadLocalLogBuffer() { buffer.reserve(128); }

ThreadLocalLogBuffer::~ThreadLocalLogBuffer() { 
	is_over.store(true);
	forceFlush();
}

bool ThreadLocalLogBuffer::shouldFlush() const {
  return buffer.size() >= flush_threshold;
}

void ThreadLocalLogBuffer::forceFlush() {
  if (Logger::GetGlobalLogger() == nullptr)
    return;

  if (is_flushing.load(std::memory_order_acquire)) {
    return;
  }
  is_flushing.store(true, std::memory_order_release);

  std::vector<std::string> tmp_vec;
  {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    tmp_vec.swap(buffer);
  }
  Logger::GetGlobalLogger()->flushThreadLocalBuffer(tmp_vec);

  is_flushing.store(false, std::memory_order_release);
}

void ThreadLocalLogBuffer::push(const std::string &msg) {
  std::lock_guard<std::mutex> lock(buffer_mutex);
  buffer.push_back(msg);
}

// ThreadLocalLogBufferGuard 实现
ThreadLocalLogBufferGuard::ThreadLocalLogBufferGuard()
    : buffer_(std::make_shared<ThreadLocalLogBuffer>()) {
  if (Logger::GetGlobalLogger() != nullptr) {
    Logger::GetGlobalLogger()->registerThreadLocalBuffer(getThreadId(), buffer_);
  }
}

ThreadLocalLogBufferGuard::~ThreadLocalLogBufferGuard() {
}

bool ThreadLocalLogBufferGuard::shouldFlush() const {
  return buffer_->shouldFlush();
}

void ThreadLocalLogBufferGuard::forceFlush() { buffer_->forceFlush(); }

void ThreadLocalLogBufferGuard::push(const std::string &msg) {
  buffer_->push(msg);
}

ThreadLocalLogBufferGuard *ThreadLocalLogBufferGuard::getGuard() {
  // Thread-local log buffer (每个线程独立的日志缓冲区)
  static thread_local ThreadLocalLogBufferGuard t_thread_log_buffer;
  return &t_thread_log_buffer;
}

} // namespace rocket
