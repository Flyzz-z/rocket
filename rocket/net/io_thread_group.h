#ifndef ROCKET_NET_IO_THREAD_GROUP_H
#define ROCKET_NET_IO_THREAD_GROUP_H

#include <vector>
#include "rocket/logger/log.h"
#include "rocket/net/io_thread.h"



namespace rocket {

class IOThreadGroup {

 public:
  IOThreadGroup(int size);

  ~IOThreadGroup();

  void start();

  void join();

  IOThread* getIOThread();

 private:

  int size_ {0};
  std::vector<IOThread*> io_thread_groups_;
	
  int index_ {0};

};

}


#endif