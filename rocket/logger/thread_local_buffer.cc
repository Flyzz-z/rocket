#include "rocket/logger/thread_local_buffer.h"
#include "rocket/logger/log.h"
#include "rocket/common/util.h"
#include <atomic>
#include <mutex>

namespace rocket {

ThreadLocalLogBuffer::ThreadLocalLogBuffer() { buffer.reserve(128); }

ThreadLocalLogBuffer::~ThreadLocalLogBuffer() { 
	is_done.store(true,std::memory_order_release);
}

void ThreadLocalLogBuffer::forceFlush() {
  if (Logger::GetGlobalLogger() == nullptr)
    return;

  std::vector<std::string> tmp_vec;
  {
    std::lock_guard<std::mutex> lock(buffer_mutex);
    tmp_vec.swap(buffer);
  }
  Logger::GetGlobalLogger()->flushThreadLocalBuffer(tmp_vec);
}

void ThreadLocalLogBuffer::push(const std::string &msg) {
  std::scoped_lock<std::mutex> lock(buffer_mutex);
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

ThreadLocalLogBufferGuard *ThreadLocalLogBufferGuard::getGuard() {
  // Thread-local log buffer (每个线程独立的日志缓冲区)
  static thread_local ThreadLocalLogBufferGuard t_thread_log_buffer;
  return &t_thread_log_buffer;
}

} // namespace rocket
