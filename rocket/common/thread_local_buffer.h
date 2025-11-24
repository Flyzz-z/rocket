#ifndef ROCKET_COMMON_THREAD_LOCAL_BUFFER_H
#define ROCKET_COMMON_THREAD_LOCAL_BUFFER_H

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace rocket {

// Forward declaration
class Logger;

// Thread-local log buffer structure
// 线程本地日志缓冲区，使用 RAII 保证线程退出时自动刷新
struct ThreadLocalLogBuffer {
  std::vector<std::string> buffer;      // 日志缓冲区
	std::atomic<bool> is_done{false};
	std::mutex buffer_mutex;

  ThreadLocalLogBuffer();

  ~ThreadLocalLogBuffer();

  void forceFlush();

  void push(const std::string& msg);

  // 禁止拷贝和移动
  ThreadLocalLogBuffer(const ThreadLocalLogBuffer&) = delete;
  ThreadLocalLogBuffer& operator=(const ThreadLocalLogBuffer&) = delete;
};

// ThreadLocalLogBuffer 的 RAII 包装器
struct ThreadLocalLogBufferGuard {
  ThreadLocalLogBufferGuard();
	~ThreadLocalLogBufferGuard();

  static ThreadLocalLogBufferGuard* getGuard();

  std::shared_ptr<ThreadLocalLogBuffer> buffer_;
};

} // namespace rocket

#endif
