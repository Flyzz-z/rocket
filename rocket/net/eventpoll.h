#ifndef ROCKET_NET_EVENTPOLL_H
#define ROCKET_NET_EVENTPOLL_H 


//todo 实现基于io_context的eventpoll，实现添加协程事件，添加定时事件, 并替换原始io_context
#include <asio/awaitable.hpp>
#include <asio/io_context.hpp>
namespace rocket {

class EventPoll { 
public:
	EventPoll();
	~EventPoll();
	void run();
	void stop();
	void addCoroutine(std::function<asio::awaitable<void>()> cb);
	void addTimer(int interval_ms, bool isRepeat ,std::function<void()> cb);
private:
	asio::io_context m_io_context;
};
}


#endif