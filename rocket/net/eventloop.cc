#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <string.h>
#include "rocket/net/eventloop.h"
#include "rocket/common/log.h"
#include "rocket/common/util.h"


#define ADD_TO_EPOLL() \
    auto it = m_listen_fds.find(event->getFd()); \
    int op = EPOLL_CTL_ADD; \
    if (it != m_listen_fds.end()) { \
      op = EPOLL_CTL_MOD; \
    } \
    epoll_event tmp = event->getEpollEvent(); \
    INFOLOG("epoll_event.events = %d", (int)tmp.events); \
    int rt = epoll_ctl(m_epoll_fd, op, event->getFd(), &tmp); \
    if (rt == -1) { \
      ERRORLOG("failed epoll_ctl when add fd, errno=%d, error=%s", errno, strerror(errno)); \
    } \
    m_listen_fds.insert(event->getFd()); \
    DEBUGLOG("add event success, fd[%d]", event->getFd()) \


#define DELETE_TO_EPOLL() \
    auto it = m_listen_fds.find(event->getFd()); \
    if (it == m_listen_fds.end()) { \
      return; \
    } \
    int op = EPOLL_CTL_DEL; \
    epoll_event tmp = event->getEpollEvent(); \
    int rt = epoll_ctl(m_epoll_fd, op, event->getFd(), NULL); \
    if (rt == -1) { \
      ERRORLOG("failed epoll_ctl when add fd, errno=%d, error=%s", errno, strerror(errno)); \
    } \
    m_listen_fds.erase(event->getFd()); \
    DEBUGLOG("delete event success, fd[%d]", event->getFd()); \

namespace rocket {

static thread_local EventLoop* t_current_eventloop = NULL;
static int g_epoll_max_timeout = 10000;
static int g_epoll_max_events = 10;

EventLoop::EventLoop() {
  if (t_current_eventloop != NULL) {
    ERRORLOG("failed to create event loop, this thread has created event loop");
    exit(0);
  }
  auto thread_id = getThreadId();

  INFOLOG("succ create event loop in thread %d", thread_id);
  t_current_eventloop = this;
}

EventLoop::~EventLoop() {
	m_io_context.stop();
}


void EventLoop::initTimer() {
  // m_timer = new Timer();
  // addEvent(m_timer);
}

void EventLoop::addTimerEvent(TimerEvent::s_ptr event) {
  // m_timer->addTimerEvent(event);
}

void EventLoop::initWakeUpFdEevent() {
  // m_wakeup_fd = eventfd(0, EFD_NONBLOCK);
  // if (m_wakeup_fd < 0) {
  //   ERRORLOG("failed to create event loop, eventfd create error, error info[%d]", errno);
  //   exit(0);
  // }
  // INFOLOG("wakeup fd = %d", m_wakeup_fd);

  // m_wakeup_fd_event = new WakeUpFdEvent(m_wakeup_fd);

  // m_wakeup_fd_event->listen(FdEvent::IN_EVENT, [this]() {
  //   char buf[8];
  //   while(read(m_wakeup_fd, buf, 8) != -1 && errno != EAGAIN) {
  //   }
  //   DEBUGLOG("read full bytes from wakeup fd[%d]", m_wakeup_fd);
  // });

  // addEvent(m_wakeup_fd_event);

}


// 事件循环函数，负责不断循环以处理各种事件，如I/O事件和定时事件
void EventLoop::loop() {
  m_is_looping = true;
  while(!m_stop_flag) {
    // 加锁以保护任务队列
    ScopeMutex<Mutex> lock(m_mutex); 
    std::queue<std::function<void()>> tmp_tasks; 
    // 交换任务队列，以在不持有锁的情况下处理任务
    m_pending_tasks.swap(tmp_tasks); 
    lock.unlock();

    // 处理所有待处理的任务
    while (!tmp_tasks.empty()) {
      std::function<void()> cb = tmp_tasks.front();
      tmp_tasks.pop();
      if (cb) {
        cb();
      }
    }

    // 如果有定时任务需要执行，那么执行
    // 1. 怎么判断一个定时任务需要执行？ （now() > TimerEvent.arrtive_time）
    // 2. arrtive_time 如何让 eventloop 监听

    // 设置epoll等待时间
    int timeout = g_epoll_max_timeout; 
    epoll_event result_events[g_epoll_max_events];
    // 开始epoll_wait等待事件发生
    int rt = epoll_wait(m_epoll_fd, result_events, g_epoll_max_events, timeout);
    // 结束epoll_wait

    // 如果epoll_wait出错，记录错误日志
    if (rt < 0) {
      ERRORLOG("epoll_wait error, errno=%d, error=%s", errno, strerror(errno));
    } else {
      // 处理所有发生的事件
      for (int i = 0; i < rt; ++i) {
        epoll_event trigger_event = result_events[i];
        FdEvent* fd_event = static_cast<FdEvent*>(trigger_event.data.ptr);
        if (fd_event == NULL) {
          ERRORLOG("fd_event = NULL, continue");
          continue;
        }

        // 处理读事件
        if (trigger_event.events & EPOLLIN) { 
          addTask(fd_event->handler(FdEvent::IN_EVENT));
        }
        // 处理写事件
        if (trigger_event.events & EPOLLOUT) { 
          addTask(fd_event->handler(FdEvent::OUT_EVENT));
        }

        // 处理错误事件
        if (trigger_event.events & EPOLLERR) {
          DEBUGLOG("fd %d trigger EPOLLERROR event", fd_event->getFd())
          deleteEvent(fd_event);
          if (fd_event->handler(FdEvent::ERROR_EVENT) != nullptr) {
            DEBUGLOG("fd %d add error callback", fd_event->getFd())
            addTask(fd_event->handler(FdEvent::OUT_EVENT));
          }
        }
      }
    }
    
  }

}

void EventLoop::wakeup() {
  INFOLOG("WAKE UP");
  m_wakeup_fd_event->wakeup();
}

void EventLoop::stop() {
  m_stop_flag = true;
  wakeup();
}

void EventLoop::dealWakeup() {

}

void EventLoop::addEvent(FdEvent* event) {
  // if (isInLoopThread()) {
  //   ADD_TO_EPOLL();
  // } else {
  //   auto cb = [this, event]() {
  //     ADD_TO_EPOLL();
  //   };
  //   addTask(cb, true);
  // }

}

void EventLoop::deleteEvent(FdEvent* event) {
  // if (isInLoopThread()) {
  //   DELETE_TO_EPOLL();
  // } else {

  //   auto cb = [this, event]() {
  //     DELETE_TO_EPOLL();
  //   };
  //   addTask(cb, true);
  // }

}

void EventLoop::addTask(std::function<void()> cb, bool is_wake_up /*=false*/) {
  // ScopeMutex<Mutex> lock(m_mutex);
  // m_pending_tasks.push(cb);
  // lock.unlock();

  // if (is_wake_up) {
  //   wakeup();
  // }
}

// bool EventLoop::isInLoopThread() {
//   return getThreadId() == m_thread_id;
// }


EventLoop* EventLoop::GetCurrentEventLoop() {
  if (t_current_eventloop) {
    return t_current_eventloop;
  }
  t_current_eventloop = new EventLoop();
  return t_current_eventloop;
}


bool EventLoop::isLooping() {
  return m_is_looping;
}

}
