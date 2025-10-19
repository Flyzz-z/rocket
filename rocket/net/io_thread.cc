
#include <asio/io_context.hpp>
#include <pthread.h>
#include <assert.h>
#include <thread>
#include "rocket/net/io_thread.h"
#include "event_loop.h"
#include "rocket/common/log.h"
#include "rocket/common/util.h"


namespace rocket {

IOThread::IOThread(): init_semaphore_(0),start_semaphore_(0) {  

  thread_ = std::thread(Main, this);
  init_semaphore_.acquire();

  DEBUGLOG("IOThread [%d] create success", thread_id_);
}
  
IOThread::~IOThread() {
  event_loop_.stop();
  thread_.join();
}

EventLoop* IOThread::getEventLoop() {
  return &event_loop_;
}

/*
* IOThread::Main
* 在新线程中执行io_context.run()
*/
void* IOThread::Main(void* arg) {
  IOThread* io_thread = static_cast<IOThread*> (arg);
  io_thread->thread_id_ = getThreadId();


  io_thread->init_semaphore_.release();
	
  DEBUGLOG("IOThread %d created, wait start semaphore", io_thread->thread_id_);

  io_thread->start_semaphore_.acquire();
  DEBUGLOG("IOThread %d start loop ", io_thread->thread_id_);
	auto work_guard = asio::make_work_guard(io_thread->event_loop_.getIOContext());
  io_thread->event_loop_.run();

  DEBUGLOG("IOThread %d end loop ", io_thread->thread_id_);

  return NULL;

}



void IOThread::start() {
  DEBUGLOG("Now invoke IOThread %d", thread_id_);
  start_semaphore_.release();
}


void IOThread::join() {
  thread_.join();
}

void IOThread::stop() {
  event_loop_.stop();
}

}