#include "rocket/common/log.h"
#include "rocket/common/config.h"
#include "rocket/common/run_time.h"
#include "rocket/common/util.h"
#include <assert.h>
#include <mutex>
#include <signal.h>
#include <sstream>
#include <stdio.h>
#include <sys/time.h>
#include <iostream>

namespace rocket {

static Logger *g_logger = NULL;

void CoredumpHandler(int signal_no) {
  ERRORLOG("progress received invalid signal, will exit");
  g_logger->flush();
	g_logger->getAsyncLopger()->thread_.join();
  g_logger->getAsyncAppLopger()->thread_.join();

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
      Config::GetGlobalConfig()->log_file_name_ + "_rpc",
      Config::GetGlobalConfig()->log_file_path_,
      Config::GetGlobalConfig()->log_max_file_size_);

  asnyc_app_logger_ = std::make_shared<AsyncLogger>(
      Config::GetGlobalConfig()->log_file_name_ + "_app",
      Config::GetGlobalConfig()->log_file_path_,
      Config::GetGlobalConfig()->log_max_file_size_);
}

void Logger::flush() {
  syncLoop();
  asnyc_logger_->stop();
  asnyc_logger_->flush();

  asnyc_app_logger_->stop();
  asnyc_app_logger_->flush();
}

void Logger::init() {
  if (type_ == 0) {
    return;
  }
  // timer_event_ =
  // std::make_shared<TimerEvent>(Config::GetGlobalConfig()->log_sync_inteval_,
  // true, std::bind(&Logger::syncLoop, this));
  // EventLoop::GetCurrentEventLoop()->addTimerEvent(timer_event_);
  // signal(SIGSEGV, CoredumpHandler);
  // signal(SIGABRT, CoredumpHandler);
  signal(SIGTERM, CoredumpHandler);
  signal(SIGKILL, CoredumpHandler);
  signal(SIGINT, CoredumpHandler);
  signal(SIGSTKFLT, CoredumpHandler);
}

void Logger::syncLoop() {
  // 同步 buffer_ 到 async_logger 的buffer队尾
  // printf("sync to async logger\n");
  std::vector<std::string> tmp_vec;
  std::unique_lock<std::mutex> lock(mutex_);
  tmp_vec.swap(buffer_);
  lock.unlock();

  if (!tmp_vec.empty()) {
    asnyc_logger_->pushLogBuffer(tmp_vec);
  }
  tmp_vec.clear();

  // 同步 app_buffer_ 到 app_async_logger 的buffer队尾
  std::vector<std::string> tmp_vec2;
  std::unique_lock<std::mutex> lock2(app_mutex_);
  tmp_vec2.swap(app_buffer_);
  lock2.unlock();

  if (!tmp_vec2.empty()) {
    asnyc_app_logger_->pushLogBuffer(tmp_vec2);
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

std::string LogEvent::toString() {
  struct timeval now_time;

  gettimeofday(&now_time, nullptr);

  struct tm now_time_t;
  localtime_r(&(now_time.tv_sec), &now_time_t);

  char buf[128];
  strftime(&buf[0], 128, "%y-%m-%d %H:%M:%S", &now_time_t);
  std::string time_str(buf);
  int ms = now_time.tv_usec / 1000;
  time_str = time_str + "." + std::to_string(ms);

  pid_ = getPid();
  thread_id_ = getThreadId();

  std::stringstream ss;

  ss << "[" << LogLevelToString(level_) << "]\t"
     << "[" << time_str << "]\t"
     << "[" << pid_ << ":" << thread_id_ << "]\t";

  // 获取当前线程处理的请求的 msgid

  std::string msgid = RunTime::GetRunTime()->msgid_;
  std::string method_name = RunTime::GetRunTime()->method_name_;
  if (!msgid.empty()) {
    ss << "[" << msgid << "]\t";
  }

  if (!method_name.empty()) {
    ss << "[" << method_name << "]\t";
  }
  return ss.str();
}

void Logger::pushLog(const std::string &msg) {
  if (type_ == 0) {
    std::cout<<msg.c_str()<<std::endl;
    return;
  }
  std::unique_lock lock(mutex_);
  buffer_.push_back(msg);
}

void Logger::pushAppLog(const std::string &msg) {
  std::unique_lock<std::mutex> lock(app_mutex_);
  app_buffer_.push_back(msg);
}

void Logger::log() {}

AsyncLogger::AsyncLogger(const std::string &file_name,
                         const std::string &file_path, int max_size)
    : file_name_(file_name), file_path_(file_path), max_file_size_(max_size),
      sempahore_(0) {

	thread_ = std::thread(&AsyncLogger::Loop, this);
  // assert(pthread_cond_init(&condtion_, NULL) == 0);
  sempahore_.acquire();
}

void *AsyncLogger::Loop(void *arg) {
  // 将 buffer
  // 里面的全部数据打印到文件中，然后线程睡眠，直到有新的数据再重复这个过程

  AsyncLogger *logger = reinterpret_cast<AsyncLogger *>(arg);

  logger->sempahore_.release();

  while (!logger->buffer_.empty()) {
    std::unique_lock<std::mutex> lock(logger->mutex_);
    if (logger->buffer_.empty()) {
      logger->cond_.wait(
          lock, [logger]() -> bool { return !logger->buffer_.empty() || logger->stop_flag_; });
    }

		if (logger->buffer_.empty()) {
      return NULL;
    }

		// 每次取一组元素打印
    std::vector<std::string> tmp;
    tmp.swap(logger->buffer_.front());
    logger->buffer_.pop();

    lock.unlock();

    timeval now;
    gettimeofday(&now, NULL);

    struct tm now_time;
    localtime_r(&(now.tv_sec), &now_time);

    const char *format = "%Y%m%d";
    char date[32];
    strftime(date, sizeof(date), format, &now_time);

    if (std::string(date) != logger->date_) {
      logger->no_ = 0;
      logger->reopen_flag_ = true;
      logger->date_ = std::string(date);
    }
    if (logger->file_hanlder_ == NULL) {
      logger->reopen_flag_ = true;
    }

    std::stringstream ss;
    ss << logger->file_path_ << logger->file_name_ << "_" << std::string(date)
       << "_log.";
    std::string log_file_name = ss.str() + std::to_string(logger->no_);

    if (logger->reopen_flag_) {
      if (logger->file_hanlder_) {
        fclose(logger->file_hanlder_);
      }
      logger->file_hanlder_ = fopen(log_file_name.c_str(), "a");
      logger->reopen_flag_ = false;
    }

    if (ftell(logger->file_hanlder_) > logger->max_file_size_) {
      fclose(logger->file_hanlder_);

      log_file_name = ss.str() + std::to_string(logger->no_++);
      logger->file_hanlder_ = fopen(log_file_name.c_str(), "a");
      logger->reopen_flag_ = false;
    }

    for (auto &i : tmp) {
      if (!i.empty()) {
        fwrite(i.c_str(), 1, i.length(), logger->file_hanlder_);
      }
    }
    fflush(logger->file_hanlder_);
  }

  return NULL;
}

void AsyncLogger::stop() { stop_flag_ = true; cond_.notify_all();}

void AsyncLogger::flush() {
  if (file_hanlder_) {
    fflush(file_hanlder_);
  }
}

void AsyncLogger::pushLogBuffer(std::vector<std::string> &vec) {
  std::unique_lock<std::mutex> lock(mutex_);
  buffer_.push(vec);
	lock.unlock();
  cond_.notify_one();
  // 这时候需要唤醒异步日志线程
  // printf("pthread_cond_signal\n");
}

} // namespace rocket