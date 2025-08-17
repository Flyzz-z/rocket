#ifndef ROCKET_NET_IO_THREAD_SINGLETON_H
#define ROCKET_NET_IO_THREAD_SINGLETON_H 


#include "rocket/common/singleton.h"
#include "rocket/net/io_thread.h"
#include <asio/io_context.hpp>

namespace rocket {
	class IOThreadSingleton: public Singleton<IOThreadSingleton>,public IOThread {
	};
}


#endif