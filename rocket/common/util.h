#ifndef ROCKET_COMMON_UTIL_H
#define ROCKET_COMMON_UTIL_H

#include <sys/types.h>
#include <unistd.h>
#include <string>

namespace rocket {

pid_t getPid();

pid_t getThreadId();

int64_t getNowMs();

int32_t getInt32FromNetByte(const char* buf);

// 创建目录（递归创建，类似 mkdir -p）
bool createDirectory(const std::string& path);

}

#endif