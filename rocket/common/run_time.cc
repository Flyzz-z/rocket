
#include "rocket/common/run_time.h"
#include <memory>

namespace rocket {


thread_local std::unique_ptr<RunTime> t_run_time = nullptr;

RunTime* RunTime::GetRunTime() {
  if (t_run_time) {
    return t_run_time.get();
  }
  t_run_time = std::make_unique<RunTime>();
  return t_run_time.get();
}


RpcInterface* RunTime::getRpcInterface() {
  return rpc_interface_;
}

}