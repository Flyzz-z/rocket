#include "rocket/common/log.h"
#include "rocket/common/run_time.h"
#include "rocket/common/util.h"
#include <sstream>
#include <sys/time.h>

namespace rocket {

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

} // namespace rocket
