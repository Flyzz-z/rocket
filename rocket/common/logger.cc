#include "rocket/common/config.h"
#include "rocket/common/log.h"
#include "rocket/common/util.h"
#include "thread_local_buffer.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <stdio.h>
#include <thread>
#include <unistd.h>

namespace rocket {

static Logger *g_logger = nullptr;

// 防止信号处理函数重入
static std::atomic<bool> in_signal_handler{false};

void CoredumpHandler(int signal_no) {
  // 防止递归调用
  bool expected = false;
  if (!in_signal_handler.compare_exchange_strong(expected, true)) {
    // 已经在处理信号，直接退出避免递归
    signal(signal_no, SIG_DFL);
    raise(signal_no);
    return;
  }

  // 不使用 ERRORLOG，避免在信号处理中触发复杂操作
  const char *msg = "Received fatal signal, flushing logs...\n";
  write(STDERR_FILENO, msg, strlen(msg));

  if (g_logger != nullptr) {
    try {
      g_logger->flush();
      g_logger->getAsyncLopger()->thread_.join();
    } catch (...) {
      // 忽略异常，避免在信号处理中崩溃
    }
  }

  signal(signal_no, SIG_DFL);
  raise(signal_no);
}

Logger *Logger::GetGlobalLogger() { return g_logger; }

Logger::Logger(LogLevel level, int type /*=1*/)
    : set_level_(level), type_(type) {

  if (type_ == 0) {
    return;
  }
  asnyc_logger_ = std::make_shared<AsyncLogger>(
      Config::GetGlobalConfig()->log_file_name_,
      Config::GetGlobalConfig()->log_file_path_,
      Config::GetGlobalConfig()->log_max_file_size_);
}

Logger::~Logger() {
  // 确保定时线程正确停止
  timer_stop_flag_.store(true);
  if (timer_thread_.joinable()) {
    timer_thread_.join();
  }
}

void Logger::flush() {
  if (type_ == 0) {
    return;
  }

  // 停止定时线程
  timer_stop_flag_.store(true);
  if (timer_thread_.joinable()) {
    timer_thread_.join();
  }

  syncLoop();
  asnyc_logger_->stop();
  asnyc_logger_->flush();
}

void Logger::init() {
  if (type_ == 0) {
    return;
  }

  signal(SIGSEGV, CoredumpHandler); // 段错误
  signal(SIGABRT, CoredumpHandler); // abort() 调用
  signal(SIGBUS, CoredumpHandler);  // 总线错误
  signal(SIGFPE, CoredumpHandler);  // 浮点异常
  signal(SIGILL, CoredumpHandler);  // 非法指令
  signal(SIGTERM, CoredumpHandler); // 终止信号
  signal(SIGINT, CoredumpHandler);  // Ctrl+C

  // 启动定时处理线程
  timer_stop_flag_.store(false);
  timer_thread_ = std::thread(&Logger::timerLoop, this);
}

void Logger::syncLoop() {
  // 同步 buffer_ 到 async_logger 的buffer队尾
  std::vector<std::string> tmp_vec;
  {
    std::scoped_lock<std::mutex> lock(mutex_);
    tmp_vec.swap(buffer_);
  }

  if (!tmp_vec.empty()) {
    asnyc_logger_->pushLogBuffer(tmp_vec);
  }
}

void Logger::pollThreadLocalBuffer() {

  if (cache_is_changed_.load()) {
    // 重新构建缓存
    std::scoped_lock<std::mutex> lock(register_threads_mutex_);
    register_threads_cache_ = register_threads_;
    cache_is_changed_.store(false);
  }

  // 批量收集所有需要刷新的日志
  std::vector<std::string> batch_logs;
  batch_logs.reserve(512); // 预分配空间

  // 轮询线程本地缓冲区，批量获取数据，获取cache副本

  bool have_thread_done = false;
  for (auto it = register_threads_cache_.begin();
       it != register_threads_cache_.end();) {
    auto t_buffer = it->second;

    std::vector<std::string> tmp_vec;
    {
      std::scoped_lock<std::mutex> lock(t_buffer->buffer_mutex);
      tmp_vec.swap(t_buffer->buffer);
    }

    // 存在缓冲区对应线程已结束的情况
    if (t_buffer->is_done.load(std::memory_order_acquire)) {
      have_thread_done = true;
    }

    // 合并到批量日志中
    batch_logs.insert(batch_logs.end(), std::make_move_iterator(tmp_vec.begin()), std::make_move_iterator(tmp_vec.end()));
    ++it;
  }

  // 一次性批量写入Logger的buffer_（只获取一次锁）
  if (!batch_logs.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.insert(buffer_.end(), std::make_move_iterator(batch_logs.begin()),std::make_move_iterator(batch_logs.end()));
  }

  // 如果存在线程已结束，需要清理
  if (have_thread_done) {
    std::lock_guard<std::mutex> lock(register_threads_mutex_);
    for (auto it = register_threads_.begin(); it != register_threads_.end();) {
      if (it->second->is_done.load(std::memory_order_acquire)) {
        it = register_threads_.erase(it);
      } else {
        ++it;
      }
    }
    cache_is_changed_.store(true);
  }
}

void Logger::timerLoop() {
  while (!timer_stop_flag_.load()) {
    // 休眠 200ms
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 轮询所有线程本地缓冲区
    pollThreadLocalBuffer();

    // 将 buffer_ 同步到 async_logger
    syncLoop();
  }
}

void Logger::InitGlobalLogger(int type /*=1*/) {

  LogLevel global_log_level =
      StringToLogLevel(Config::GetGlobalConfig()->log_level_);
  printf("Init log level [%s]\n", LogLevelToString(global_log_level).c_str());
  g_logger = new Logger(global_log_level, type);
  g_logger->init();
}

std::string LogLevelToString(LogLevel level) {
  switch (level) {
  case Debug:
    return "DEBUG";

  case Info:
    return "INFO";

  case Error:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}

LogLevel StringToLogLevel(const std::string &log_level) {
  if (log_level == "DEBUG") {
    return Debug;
  } else if (log_level == "INFO") {
    return Info;
  } else if (log_level == "ERROR") {
    return Error;
  } else {
    return Unknown;
  }
}

void Logger::pushLog(const std::string &msg) {
  if (type_ == 0) {
    std::cout << msg.c_str() << std::endl;
    return;
  }

  // 写入线程本地缓冲区
  auto t_buffer_guard = ThreadLocalLogBufferGuard::getGuard();
  t_buffer_guard->buffer_->push(msg);
}

void Logger::flushThreadLocalBuffer(
    const std::vector<std::string> &thread_buffer) {
  // 批量刷新日志
  if (!thread_buffer.empty()) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.insert(buffer_.end(), thread_buffer.begin(), thread_buffer.end());
  }
}

void Logger::log() {}

void Logger::registerThreadLocalBuffer(
    pid_t tid, std::shared_ptr<ThreadLocalLogBuffer> buffer) {
  std::lock_guard<std::mutex> lock(register_threads_mutex_);
  register_threads_[tid] = buffer;
  cache_is_changed_.store(true,std::memory_order_release);
}

void Logger::unregisterThreadLocalBuffer(pid_t tid) {
  std::lock_guard<std::mutex> lock(register_threads_mutex_);
  register_threads_.erase(tid);
  cache_is_changed_.store(true,std::memory_order_release);
}
} // namespace rocket
