#ifndef ROCKET_COMMON_EXCEPTION_H
#define ROCKET_COMMON_EXCEPTION_H

#include <exception>
#include <string>

namespace rocket {

class RocketException : public std::exception {
 public:

  RocketException(int error_code, const std::string& error_info) : error_code_(error_code), error_info_(error_info) {}

  // 异常处理
  // 当捕获到 RocketException 及其子类对象的异常时，会执行该函数
  virtual void handle() = 0;

  virtual ~RocketException() {};

  int errorCode() {
    return error_code_;
  }

  std::string errorInfo() {
    return error_info_;
  }

 protected:
  int error_code_ {0};

  std::string error_info_;

};


}



#endif