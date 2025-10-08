# asio IO多路复用

## OBJECT

1. 调度流程
2. epoll封装
3. 协程封装

## 调度流程

### io_context.run

run函数中不断调用do_run_one返回处理的任务数量

```c++
  std::size_t n = 0;
  for (; do_run_one(lock, this_thread, ec); lock.lock())
    if (n != (std::numeric_limits<std::size_t>::max)())
      ++n;
```

在do_run_one中：

> scheduler(io_context)中有两类队列，1. 全局队列op_queue,使用互斥锁进行保护。 2. 线程私有队列，无锁。

1. 主循环: 函数在一个while循环中执行，只要scheduler没有停止（stopped_为false）就会继续执行。

2. 操作队列处理:

   如果操作队列(op_queue_)不为空，则取出第一个操作进行处理
   如果取出的操作是任务操作(task_operation_)，则运行相关任务操作
   如果是普通操作，则完成该操作并返回

3. 任务操作处理:
   创建任务清理对象并在作用域结束时自动清理
   调用task_->run()执行任务

4. 普通操作处理:
   获取任务结果
   创建工作清理对象确保工作计数正确递减
   调用操作的complete()方法完成操作

5. 空队列等待:
   当操作队列为空时，根据wait_usec_的值决定行为:
   如果为0，则短暂解锁再加锁
   如果大于0，则等待指定微秒数
   如果小于0，则无限期等待直到被唤醒

6. 结束条件: 当scheduler被停止时，函数返回0



可以看到队列不为空时分两类操作进行处理，普通操作实际就是可执行的对象，执行即可。任务操作实际时执行

task\_->run，task\_为scheduler_task类型，包括epoll,kqueue,io_uring等实现，我们主要关注epoll的实现。此外操作处理后都会有清理操作，比如两种类型操作都有的将线程私有队列中的op放入全局队列，还有任务操作独有的将任务操作重新加入全局队列（不断触发epoll_wait）。

### epoll_reactor

```c++
class scheduler_task
{
public:
  // Run the task once until interrupted or events are ready to be dispatched.
  virtual void run(long usec, op_queue<scheduler_operation>& ops) = 0;

  // Interrupt the task.
  virtual void interrupt() = 0;

protected:
  // Prevent deletion through this type.
  ~scheduler_task()
  {
  }
};
```

epoll_reactor类型为epoll的scheduler_task实现，其run函数主要流程：

1. **计算超时时间**：根据传入的微秒数 `usec` 和定时器状态确定 `epoll_wait` 的超时值。当前计时器队列中最快到期的timer间隔。（若使用timerfd,则不需要，只需处理time事件即可）

2. **阻塞等待事件**：调用 `epoll_wait` 等待最多 128 个 I/O 事件，设置超时时间。

3. **事件跟踪（可选）**：若启用跟踪，则记录发生的事件类型（读、写、错误）。

4. **分发事件**

   ：

   > 中断，其实就是添加一个事件，event中私有数据结构设置为中断
   >
   > void epoll_reactor::interrupt()
   >
   > {
   >
   >  epoll_event ev = { 0, { 0 } };
   >
   >  ev.events = EPOLLIN | EPOLLERR | EPOLLET;
   >
   >  ev.data.ptr = &interrupter_;
   >
   >  epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, interrupter_.read_descriptor(), &ev);
   >
   > }

   - 若为中断事件或定时器事件，则标记需要检查定时器。
   - 否则将就绪的描述符操作加入调度队列（该调度队列为线程本地队列）。

5. **处理定时器**：如有定时器到期，收集就绪的定时器任务，并更新 timerfd（如使用，使用一个timerfd来通知是否有定时任务到期），将到期定时任务放入线程本地队列。

#### operaion

epoll实际会为每个事件创建operation并放入线程私有队列，该operation实际是epoll_reactor中定义的descriptor_state。

```
  struct descriptor_state : operation
  {
    descriptor_state* next_;
    descriptor_state* prev_;

    mutex mutex_;
    epoll_reactor* reactor_;
    int descriptor_;
    uint32_t registered_events_;
    op_queue<reactor_op> op_queue_[max_ops];
    bool try_speculative_[max_ops];
    bool shutdown_;

    ASIO_DECL descriptor_state(bool locking, int spin_count);
    void set_ready_events(uint32_t events) { task_result_ = events; }
    void add_ready_events(uint32_t events) { task_result_ |= events; }
    ASIO_DECL operation* perform_io(uint32_t events);
    ASIO_DECL static void do_complete(
        void* owner, operation* base,
        const asio::error_code& ec, std::size_t bytes_transferred);
  };
```

descriptor_state为普通操作,会被从全局队列取出，执行时会调用do_complete函数。do_complete其实就是完成I/O操作并调用回调函数。I/O操作实际上存在一个针对不同事件类型的reactor_op队列，也就是说支持多线程对同一个描述符的操作，实际上是串行排队执行。

```
void epoll_reactor::descriptor_state::do_complete(
    void* owner, operation* base,
    const asio::error_code& ec, std::size_t bytes_transferred)
{
  if (owner)
  {
    descriptor_state* descriptor_data = static_cast<descriptor_state*>(base);
    uint32_t events = static_cast<uint32_t>(bytes_transferred);
    // 完成I/O操作
    if (operation* op = descriptor_data->perform_io(events))
    {
  	  // 调用回调函数
      op->complete(owner, ec, 0);
    }
  }
}
```

perform_io: 处理I/O操作

```c++
operation* epoll_reactor::descriptor_state::perform_io(uint32_t events)
{
  mutex_.lock();
  perform_io_cleanup_on_block_exit io_cleanup(reactor_);
  mutex::scoped_lock descriptor_lock(mutex_, mutex::scoped_lock::adopt_lock);

  // Exception operations must be processed first to ensure that any
  // out-of-band data is read before normal data.
  static const int flag[max_ops] = { EPOLLIN, EPOLLOUT, EPOLLPRI };
  for (int j = max_ops - 1; j >= 0; --j)
  {
    if (events & (flag[j] | EPOLLERR | EPOLLHUP))
    {
      try_speculative_[j] = true;
      while (reactor_op* op = op_queue_[j].front())
      {
        if (reactor_op::status status = op->perform())
        {
          op_queue_[j].pop();
          io_cleanup.ops_.push(op);
          if (status == reactor_op::done_and_exhausted)
          {
            try_speculative_[j] = false;
            break;
          }
        }
        else
          break;
      }
    }
  }

  // The first operation will be returned for completion now. The others will
  // be posted for later by the io_cleanup object's destructor.
  io_cleanup.first_op_ = io_cleanup.ops_.front();
  io_cleanup.ops_.pop();
  return io_cleanup.first_op_;
}
```



### async_read

那么网络操作是如何注册操作的呢？以async_read举例。

实际会调用async_read_some,其中会调用async_initiate,而async_initiate中会调用async_result的静态函数initiate函数,而在这个函数中实际就是调用最开始传入的initiate函数，这里就是initiate_async_receive()。

async_read_some

```C++
 template <typename MutableBufferSequence,
      ASIO_COMPLETION_TOKEN_FOR(void (asio::error_code,
        std::size_t)) ReadToken = default_completion_token_t<executor_type>>
  auto async_read_some(const MutableBufferSequence& buffers,
      ReadToken&& token = default_completion_token_t<executor_type>())
    -> ...
  {
    return async_initiate<ReadToken,
      void (asio::error_code, std::size_t)>(
        initiate_async_receive(this), token,
        buffers, socket_base::message_flags(0));
  }
```

async_iniatiate

```C++
inline auto async_initiate(Initiation&& initiation,
    type_identity_t<CompletionToken>& token, Args&&... args)
  -> ...
{
  return async_result<decay_t<CompletionToken>, Signatures...>::initiate(
      static_cast<Initiation&&>(initiation),
      static_cast<CompletionToken&&>(token),
      static_cast<Args&&>(args)...);
}
```
async_result::initiate
```
template <typename Initiation,
  ASIO_COMPLETION_HANDLER_FOR(Signatures...) RawCompletionToken,
  typename... Args>
static return_type initiate(Initiation&& initiation,
  RawCompletionToken&& token, Args&&... args)
{
static_cast<Initiation&&>(initiation)(
    static_cast<RawCompletionToken&&>(token),
    static_cast<Args&&>(args)...);
}
```

initiate_async_receive: 实际上是一个类，提供了()的重载

```
void operator()(ReadHandler&& handler,
    const MutableBufferSequence& buffers,
    socket_base::message_flags flags) const
{
  ...
  detail::non_const_lvalue<ReadHandler> handler2(handler);
  self_->impl_.get_service().async_receive(
      self_->impl_.get_implementation(), buffers, flags,
      handler2.value, self_->impl_.get_executor());
}
```

其中又调用了async_receive: 其中比较核心的点是 

1. 构建了reactive_socket_recv_op，在代码中有一些其他步骤，应该是asio内存管理的优化，总而言之就是构建了reactive_socket_recv_op对象。 
2.  调用start_op函数，第三个参数实际就是传递的reactive_socket_recv_op，reactive_socket_recv_op实际上继承了reactor_op。

```c++
template <typename MutableBufferSequence,
      typename Handler, typename IoExecutor>
  void async_receive(base_implementation_type& impl,
      const MutableBufferSequence& buffers, socket_base::message_flags flags,
      Handler& handler, const IoExecutor& io_ex)
  {
    bool is_continuation =
      asio_handler_cont_helpers::is_continuation(handler);

    associated_cancellation_slot_t<Handler> slot
      = asio::get_associated_cancellation_slot(handler);

    //1. 构建了reactive_socket_recv_op
    // Allocate and construct an operation to wrap the handler.
    typedef reactive_socket_recv_op<
        MutableBufferSequence, Handler, IoExecutor> op;
    typename op::ptr p = { asio::detail::addressof(handler),
      op::ptr::allocate(handler), 0 };
    p.p = new (p.v) op(success_ec_, impl.socket_,
        impl.state_, buffers, flags, handler, io_ex);

   ......

    start_op(impl,
        (flags & socket_base::message_out_of_band)
          ? reactor::except_op : reactor::read_op,
        p.p, is_continuation,
        (flags & socket_base::message_out_of_band) == 0,
        ((impl.state_ & socket_ops::stream_oriented)
          && buffer_sequence_adapter<asio::mutable_buffer,
            MutableBufferSequence>::all_empty(buffers)), true, &io_ex, 0);
    p.v = p.p = 0;
  }
```

start_op该函数后续又调用了reactor_.start_op,其实就是epoll_reactor::start_op，其中会有一系列操作和优化，其中一项是满足一定条件时进行推测执行，当下就能执行成功就不用加入epoll了。如果没能推测执行，最终就会将创建的reactor_op加入到描述符数据结构 descriptor_data的读事件操作队列中。等后EPOLL_IN事件发生就会处理该op了，就如epoll_reactor章节所述。

```
  descriptor_data->op_queue_[op_type].push(op);
```



