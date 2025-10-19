#ifndef ROCKET_COMMON_LOG_H
#define ROCKET_COMMON_LOG_H

#include <condition_variable>
#include <mutex>
#include <semaphore>
#include <string>
#include <queue>
#include <memory>
#include <semaphore.h>

namespace rocket {


template<typename... Args>
std::string formatString(const char* str, Args&&... args) {

  int size = snprintf(nullptr, 0, str, args...);

  std::string result;
  if (size > 0) {
    result.resize(size);
    snprintf(&result[0], size + 1, str, args...);
  }

  return result;
}


#define DEBUGLOG(str, ...) \
  if (rocket::Logger::GetGlobalLogger()->getLogLevel() && rocket::Logger::GetGlobalLogger()->getLogLevel() <= rocket::Debug) \
  { \
    rocket::Logger::GetGlobalLogger()->pushLog(rocket::LogEvent(rocket::LogLevel::Debug).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + rocket::formatString(str, ##__VA_ARGS__) + "\n");\
  } \


#define INFOLOG(str, ...) \
  if (rocket::Logger::GetGlobalLogger()->getLogLevel() <= rocket::Info) \
  { \
    rocket::Logger::GetGlobalLogger()->pushLog(rocket::LogEvent(rocket::LogLevel::Info).toString() \
    + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + rocket::formatString(str, ##__VA_ARGS__) + "\n");\
  } \

#define ERRORLOG(str, ...) \
  if (rocket::Logger::GetGlobalLogger()->getLogLevel() <= rocket::Error) \
  { \
    rocket::Logger::GetGlobalLogger()->pushLog(rocket::LogEvent(rocket::LogLevel::Error).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + rocket::formatString(str, ##__VA_ARGS__) + "\n");\
  } \


#define APPDEBUGLOG(str, ...) \
  if (rocket::Logger::GetGlobalLogger()->getLogLevel() <= rocket::Debug) \
  { \
    rocket::Logger::GetGlobalLogger()->pushAppLog(rocket::LogEvent(rocket::LogLevel::Debug).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + rocket::formatString(str, ##__VA_ARGS__) + "\n");\
  } \


#define APPINFOLOG(str, ...) \
  if (rocket::Logger::GetGlobalLogger()->getLogLevel() <= rocket::Info) \
  { \
    rocket::Logger::GetGlobalLogger()->pushAppLog(rocket::LogEvent(rocket::LogLevel::Info).toString() \
    + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + rocket::formatString(str, ##__VA_ARGS__) + "\n");\
  } \

#define APPERRORLOG(str, ...) \
  if (rocket::Logger::GetGlobalLogger()->getLogLevel() <= rocket::Error) \
  { \
    rocket::Logger::GetGlobalLogger()->pushAppLog(rocket::LogEvent(rocket::LogLevel::Error).toString() \
      + "[" + std::string(__FILE__) + ":" + std::to_string(__LINE__) + "]\t" + rocket::formatString(str, ##__VA_ARGS__) + "\n");\
  } \



enum LogLevel {
  Unknown = 0,
  Debug = 1,
  Info = 2,
  Error = 3
};


std::string LogLevelToString(LogLevel level);

LogLevel StringToLogLevel(const std::string& log_level);

class AsyncLogger {

 public:
  typedef std::shared_ptr<AsyncLogger> s_ptr;
  AsyncLogger(const std::string& file_name, const std::string& file_path, int max_size);

  void stop();

  // 刷新到磁盘
  void flush();

  void pushLogBuffer(std::vector<std::string>& vec);


 public:
  static void* Loop(void*);

 public:
  std::thread thread_;

 private:
  // file_path_/file_name_yyyymmdd_.0

  std::queue<std::vector<std::string>> buffer_;

  std::string file_name_;    // 日志输出文件名字
  std::string file_path_;    // 日志输出路径
  int max_file_size_ {0};    // 日志单个文件最大大小, 单位为字节

  std::binary_semaphore sempahore_;

  std::condition_variable cond_;
  std::mutex mutex_;

  std::string date_;   // 当前打印日志的文件日期
  FILE* file_hanlder_ {NULL};   // 当前打开的日志文件句柄

  bool reopen_flag_ {false};

  int no_ {0};   // 日志文件序号

  bool stop_flag_ {false};

};

class Logger {
 public:
  typedef std::shared_ptr<Logger> s_ptr;

  Logger(LogLevel level, int type = 1);

  void pushLog(const std::string& msg);

  void pushAppLog(const std::string& msg);

  void init();

  void log();

  LogLevel getLogLevel() const {
    return set_level_;
  }

  AsyncLogger::s_ptr getAsyncAppLopger() {
    return asnyc_app_logger_;
  }

  AsyncLogger::s_ptr getAsyncLopger() {
    return asnyc_logger_;
  }

  void syncLoop();

  void flush();

 public:
  static Logger* GetGlobalLogger();

  static void InitGlobalLogger(int type = 1);

 private:
  LogLevel set_level_;
  std::vector<std::string> buffer_;

  std::vector<std::string> app_buffer_;

  std::mutex mutex_;

  std::mutex app_mutex_;

  // file_path_/file_name_yyyymmdd_.1

  std::string file_name_;    // 日志输出文件名字
  std::string file_path_;    // 日志输出路径
  int max_file_size_ {0};    // 日志单个文件最大大小

  AsyncLogger::s_ptr asnyc_logger_;

  AsyncLogger::s_ptr asnyc_app_logger_;
  int type_ {0};

};


class LogEvent {
 public:

  LogEvent(LogLevel level) : level_(level) {}

  std::string getFileName() const {
    return file_name_;  
  }

  LogLevel getLogLevel() const {
    return level_;
  }

  std::string toString();


 private:
  std::string file_name_;  // 文件名
  int32_t file_line_;  // 行号
  int32_t pid_;  // 进程号
  int32_t thread_id_;  // 线程号

  LogLevel level_;     //日志级别

};



}

#endif