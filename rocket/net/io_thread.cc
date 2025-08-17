
#include <asio/io_context.hpp>
#include <pthread.h>
#include <assert.h>
#include <thread>
#include "rocket/net/io_thread.h"
#include "rocket/common/log.h"
#include "rocket/common/util.h"


namespace rocket {

IOThread::IOThread(): m_io_context(1),m_init_semaphore(0),m_start_semaphore(0) {  

  m_thread = std::thread(Main, this);
  m_init_semaphore.acquire();

  DEBUGLOG("IOThread [%d] create success", m_thread_id);
}
  
IOThread::~IOThread() {
  m_io_context.stop();
  m_thread.join();
}

asio::io_context* IOThread::getIOContext() {
  return &m_io_context;
}

/*
* IOThread::Main
* 在新线程中执行io_context.run()
*/
void* IOThread::Main(void* arg) {
  IOThread* io_thread = static_cast<IOThread*> (arg);
  io_thread->m_thread_id = getThreadId();


  io_thread->m_init_semaphore.release();
	
  DEBUGLOG("IOThread %d created, wait start semaphore", io_thread->m_thread_id);

  io_thread->m_start_semaphore.acquire();
  DEBUGLOG("IOThread %d start loop ", io_thread->m_thread_id);
	auto work_guard = asio::make_work_guard(io_thread->m_io_context);
  io_thread->m_io_context.run();

  DEBUGLOG("IOThread %d end loop ", io_thread->m_thread_id);

  return NULL;

}



void IOThread::start() {
  DEBUGLOG("Now invoke IOThread %d", m_thread_id);
  m_start_semaphore.release();
}


void IOThread::join() {
  m_thread.join();
}

void IOThread::stop() {
  m_io_context.stop();
}

}