#include "rocket/net/io_thread_group.h"
#include "rocket/log/log.h"


namespace rocket {


IOThreadGroup::IOThreadGroup(int size) : size_(size) {
  io_thread_groups_.resize(size);
  for (size_t i = 0; (int)i < size; ++i) {
    io_thread_groups_[i] = new IOThread();
  }
}

IOThreadGroup::~IOThreadGroup() {

}

void IOThreadGroup::start() {
  for (size_t i = 0; i < io_thread_groups_.size(); ++i) {
    io_thread_groups_[i]->start();
  }
}

void IOThreadGroup::join() {
  for (size_t i = 0; i < io_thread_groups_.size(); ++i) {
    io_thread_groups_[i]->join();
  }
} 

IOThread* IOThreadGroup::getIOThread() {
  if (index_ == (int)io_thread_groups_.size() || index_ == -1)  {
    index_ = 0;
  }
  return io_thread_groups_[index_++];
}

}