# Rocket 
  原项目 [README](./README.md.bac)
	本项目主要添加内容:
1. 本项目基于 C++20 协程，使用 asio 网络库重构项目网络模型为基于协程的 Reactor 网络处理模型。包括重构服务端逻辑，客户端逻辑，以及 rpc 处理相关逻辑和相关数据结构。阅读 asio 协程调度代码，理解其调度流程。
2. 基于现代 C++ 提供的线程，并发相关 API 重写实现异步日志写入模块。基于 protobuf 封装实现服务间通信协议。
3. 实现基于 etcd 的服务注册和服务发现功能。包括健康检查，缓存服务，负载均衡功能。对 etcd 分布式原理有一定了解。

## 总结
[asio-IO多路复用](./doc/asio-IO多路复用.md)

## 基于C++20协程服务器

C++20协程文章

[Asymmetric Transfer  C++ Coroutines](https://lewissbaker.github.io/)

[My tutorial and take on C++20 coroutines](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html)

通过下面代码学习C++20协程实现epoll echo服务器

[coro_epoll_kqueue](https://github.com/franktea/coro_epoll_kqueue)

### C++20 协程封装

#### 前提

promise是管理协程内部周期的，协程函数返回值必须包含一个名为`promise_type`的类，实现相关函数。

awaitable对象需要实现awaiter，实现以下函数：

```C++
bool await_ready() noexcept;
//返回值：
//true：操作已完成，不需要挂起协程
//false：操作未完成，需要挂起协程


auto await_suspend(std::coroutine_handle<> coroutine);
// 或
void await_suspend(std::coroutine_handle<> coroutine);
bool await_suspend(std::coroutine_handle<> coroutine);
//参数：当前协程的句柄
//返回值：
//void：挂起协程，不立即恢复
//bool：true表示挂起协程，false表示不挂起（立即恢复）
//std::coroutine_handle<>：挂起当前协程，转而执行返回的协程句柄

auto await_resume();
// 或
void await_resume();
//返回值：co_await表达式的返回值
```

#### Promise 封装

协程函数返回值必须包含一个名为`promise_type`的类，用以管理协程内部生命周期的类，需要实现**get_return_object()**，**initial_suspend()**，**final_suspend()**，**unhandled_exception()**，**return_void() 或 return_value()**函数。

```C++
template<typename T>
struct promise_type_base {
    coroutine_handle<> continuation_ = std::noop_coroutine(); // who waits on this coroutine
    task<T> get_return_object();
    suspend_always initial_suspend() { return {}; }

    // 实现最终挂起awaiter，当前协程运行完毕，在这里回到父协程，即continuation_
    struct final_awaiter {
        bool await_ready() noexcept { return false; }
        void await_resume() noexcept {}

        template<typename promise_type>
        coroutine_handle<> await_suspend(coroutine_handle<promise_type> coro) noexcept {
            return coro.promise().continuation_;
        }
    };

    auto final_suspend() noexcept {
        return final_awaiter{};
    }

    void unhandled_exception() { //TODO: 
        std::exit(-1);
    }
}; // struct promise_type_base
```

- 包含一个延续句柄 `continuation_`，用于指向等待当前协程完成的父协程
- 实现了基本的协程生命周期管理方法：initial_suspend() 和 final_suspend() 。
- 特别是在 final_awaiter 结构中实现了协程完成后返回到父协程的逻辑await_suspend返回当前协程完成后。

继承特化，返回值为T类型和void的版本，覆盖了`await_resume()`函数，实现了co_await等待返回值的逻辑：

```C++
template<typename T>
struct promise_type final: promise_type_base<T> {
    T result;
    void return_value(T value) { result = value; }
    T await_resume() { return result; }
    task<T> get_return_object();
};

template<>
struct promise_type<void> final: promise_type_base<void> {
    void return_void() {}
    void await_resume() {}
    task<void> get_return_object();
};

template<typename T>
inline task<T> promise_type<T>::get_return_object() {
    return task<T>{ coroutine_handle<promise_type<T>>::from_promise(*this)};
}

inline task<void> promise_type<void>::get_return_object() {
    return task<void>{ coroutine_handle<promise_type<void>>::from_promise(*this)};
}
```

#### task类型封装

用作协程函数返回类型，使用using包含promise_type类，同时实现awaiter，可以使用co_awaiter等待。其中handle_协程句柄由编译器传入，代表返回值为该task的协程。函数返回值为task类型即可标记为协程函数。

```C++
template<typename T = void>
struct task {
    using promise_type = detail::promise_type<T>;
    task():handle_(nullptr){}
    task(coroutine_handle<promise_type> handle):handle_(handle){}
    bool await_ready() { return false; }
    T await_resume() {
        return handle_.promise().result;
    }

    // 这里是对当前task对象本身调用co_await，说明一定是嵌套的协程，
    // 当前所在的协程是父协程，即await_suspend参数所代表的协程，
    // 被co_await的task对象所在的协程为子协程，即当前task.handle_，为子协程。
    coroutine_handle<> await_suspend(coroutine_handle<> waiter) {
        handle_.promise().continuation_ = waiter;
        // waiter所在的协程，即当前协程挂起了，让子协程，即handle_所表示的协程恢复。子协程结束完以后又回到waiter。
        return handle_;
    }

    void resume() {
        handle_.resume(); // 继续执行到下一个co_await 或者 over
    }
	
    // 编译器生成代码时会生成协程栈帧，调用promise的get_return_object传入handle_构造task
    coroutine_handle<promise_type> handle_;
};
```
### 网络组件封装
异步系统调用封装,实现了awaiter相关函数。
1. bool await_ready() : 总是挂起
2. bool await_suspend(std::coroutine_handle<> h) : 调用一次Syscall函数，成功返回，需要等待则挂起。
3. ReturnValue await_resume() : epoll事件触发后，进行系统调用返回调用结果。
```C++
template<typename Syscall, typename ReturnValue>
class AsyncSyscall {
public:
    AsyncSyscall() : suspended_(false) {}

    bool await_ready() const noexcept { return false; }

    bool await_suspend(std::coroutine_handle<> h) noexcept {
        static_assert(std::is_base_of_v<AsyncSyscall, Syscall>);
        handle_ = h;
        value_ = static_cast<Syscall*>(this)->Syscall();
        suspended_ = value_ == -1 && (errno == EAGAIN || errno == EWOULDBLOCK);
        if(suspended_) {
            // 设置每个操作的coroutine handle，recv/send在适当的epoll事件发生后才能正常调用
            static_cast<Syscall*>(this)->SetCoroHandle();
        }
        return suspended_;
    }

    ReturnValue await_resume() noexcept {
        std::cout<<"await_resume\n";
        if(suspended_) {
            value_ = static_cast<Syscall*>(this)->Syscall();
        }
        return value_;
    }
protected:
    bool suspended_;
    // 当前awaiter所在协程的handle，需要设置给socket的coro_recv_或是coro_send_来读写数据
    // handle_不是在构造函数中设置的，所以在子类的构造函数中也无法获取，必须在await_suspend以后才能设置
    std::coroutine_handle<> handle_;
    ReturnValue value_;
};
```
具体实现包含Accept,Send,Recv，通过继承实现，并实现Syscall()和SetCoroHandle()函数。这里用Accept类为例。Accept类的Syscall()函数实际是accept系统调用，SetCoroHandle()函数实际是设置socket的coro_recv_成员变量，即接受协程（accept监听EPOLLIN事件）。 构造函数中.WatchRead(socket_)，实际上是加入epoll监听socket_的EPOLLIN事件，触发时调用socket的coro_recv_成员变量的resume()函数。
```C++
class Accept : public AsyncSyscall<Accept, int> {
public:
    Accept(Socket* socket) : AsyncSyscall{}, socket_(socket) {
        socket_->io_context_.WatchRead(socket_);
        std::cout<<" socket accept opertion\n";
    }

    ~Accept() {
        socket_->io_context_.UnwatchRead(socket_);
        std::cout<<"~socket accept operation\n";
    }

    int Syscall() {
        struct sockaddr_storage addr;
        socklen_t addr_size = sizeof(addr);
        std::cout<<"accept "<<socket_->fd_<<"\n";
        return ::accept(socket_->fd_, (struct sockaddr*)&addr, &addr_size);
    }

    void SetCoroHandle() {
        socket_->coro_recv_ = handle_;
    }
private:
    Socket* socket_;
    void* buffer_;
    std::size_t len_;
};
```
Socket中实际调用,Socket::accept协程中直接co_await Accept对象。
```C++
task<std::shared_ptr<Socket>> Socket::accept() {
    int fd = co_await Accept{this};
    if(fd == -1) {
        throw std::runtime_error{"accept error"};
    }
    co_return std::shared_ptr<Socket>(new Socket{fd, io_context_});
}
```
#### 事件循环
创建一个io_context对象，并调用Run()函数，该函数会创建一个epoll句柄，并开始监听socket_fd_，当socket_fd_有事件发生时，会调用epoll_wait()函数，并调用socket的coro_recv_或者coro_send_的resume()函数。
```C++
void IoContext::run() {
    struct epoll_event ev, events[max_events];
    for(;;) {
        int nfds = epoll_wait(fd_, events, max_events, -1);
        if(nfds == -1) {
            throw std::runtime_error{"epoll_wait"};
        }

        for(int i = 0; i < nfds; ++i) {
            auto socket = static_cast<Socket*>(events[i].data.ptr);
            if(events[i].events & EPOLLIN) {
                socket->ResumeRecv();
            }
            if(events[i].events & EPOLLOUT) {
                socket->ResumeSend();
            }
        }
    }
}
```

通过这些组件实现echo服务器可以参见代码仓库。
