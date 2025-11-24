#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <string.h>
#include <arpa/inet.h>
#include <filesystem>
#include "rocket/common/util.h"


namespace rocket {

static int g_pid = 0;

static thread_local int t_thread_id = 0;

pid_t getPid() {
  if (g_pid != 0) {
    return g_pid;
  }
  return getpid();
}

pid_t getThreadId() {
  if (t_thread_id != 0) {
    return t_thread_id;
  }
  return syscall(SYS_gettid);
}


int64_t getNowMs() {
  timeval val;
  gettimeofday(&val, NULL);

  return val.tv_sec * 1000 + val.tv_usec / 1000;

}


int32_t getInt32FromNetByte(const char* buf) {
  int32_t re;
  memcpy(&re, buf, sizeof(re));
  return ntohl(re);
}

bool createDirectory(const std::string& path) {
  if (path.empty()) {
    return false;
  }

  try {
    // std::filesystem::create_directories 会递归创建所有不存在的父目录
    // 如果目录已存在，返回 false 但不会抛异常
    std::filesystem::create_directories(path);
    return true;
  } catch (const std::filesystem::filesystem_error& e) {
    // 创建失败（权限不足、路径非法等）
    return false;
  }
}

}