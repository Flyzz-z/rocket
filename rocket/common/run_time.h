#ifndef ROCKET_COMMON_RUN_TIME_H
#define ROCKET_COMMON_RUN_TIME_H


#include <string>

namespace rocket {

class RpcInterface;

class RunTime {
 public:
  RpcInterface* getRpcInterface();

 public:
  static RunTime* GetRunTime();


 public:
  std::string msgid_;
  std::string method_name_;
  RpcInterface* rpc_interface_ {NULL};

};

}


#endif