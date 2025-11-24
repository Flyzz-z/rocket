#include "rocket/logger/log.h"
#include <cstring>
#include <sstream>
#include <sys/time.h>

namespace rocket {

AsyncLogger::AsyncLogger(const std::string &file_name,
                         const std::string &file_path, int max_size)
    : file_name_(file_name), file_path_(file_path), max_file_size_(max_size),
      sempahore_(0) {

  thread_ = std::thread(&AsyncLogger::Loop, this);
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
      logger->cond_.wait(lock, [logger]() -> bool {
        return !logger->buffer_.empty() || logger->stop_flag_;
      });
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

void AsyncLogger::stop() {
  stop_flag_ = true;
  cond_.notify_all();
}

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
}


} // namespace rocket
