# Rocket 
  原项目 [README](./README.md.bac)
	本项目主要添加内容:
1. 本项目基于 C++20 协程，使用 asio 网络库重构项目网络模型为基于协程的网络处理模型，阅读 asio io_context 调度相关代码，理解其调度流程。

（1）网络连接协程化。服务端采用一主多从的线程模型，服务端客户端网络处理均协程化。

（2）RPC逻辑协程化。RPC部分基于 Protobuf 提供相关类型实现，对 stub 进行封装使其支持协程调用，同时对 RPC 处理逻辑进行修改支持协程化。

2. 实现基于 etcd 的服务注册和服务发现功能。(1) 基于 etcd watcher 机制实现健康检查功能。(2) 实现服务缓存，使用自旋锁+分段锁对服务缓存进行并发保护，提升并发能力。(3) 实现基于轮询的负载均衡。(4) 对 etcd 分布式原理有一定了解。

3. 重构实现异步日志模块，增添线程本地队列，双缓冲+原子变量保证并发安全，提升并发能力。

4. 编写压测代码测试客户端服务端整体性能，生成火焰图分析性能，优化逻辑提升处理性能。

## 总结
[asio-IO多路复用](./docs/asio-IO多路复用.md) \
[C++20协程实现echo服务器](./docs/C++20实现echo服务器.md)

## C++20协程学习

C++20协程文章

[Asymmetric Transfer  C++ Coroutines](https://lewissbaker.github.io/)

[My tutorial and take on C++20 coroutines](https://www.scs.stanford.edu/~dm/blog/c++-coroutines.html)

## 并发设计

### 日志模块

日志模块采用**三级缓冲 + 定时轮询**的架构，实现高并发场景下的异步日志写入，最大限度减少业务线程阻塞。

#### 整体架构

```
业务线程1          业务线程2          业务线程N
    ↓                  ↓                  ↓
┌─────────┐      ┌─────────┐      ┌─────────┐
│ Thread  │      │ Thread  │      │ Thread  │
│ Local   │      │ Local   │      │ Local   │
│ Buffer1 │      │ Buffer2 │      │ BufferN │
└─────────┘      └─────────┘      └─────────┘
  (mutex)          (mutex)          (mutex)
    │                  │                  │
    └──────────────────┼──────────────────┘
                       ↓
                 Timer Thread
              (200ms 定时轮询)
                       ↓
                 ┌──────────┐
                 │  Logger  │
                 │  Buffer  │
                 └──────────┘
                   (mutex)
                       ↓
              (200ms 定时同步)
                       ↓
                ┌─────────────┐
                │ AsyncLogger │
                │   Buffer    │
                │   (queue)   │
                └─────────────┘
            (mutex + cond_var)
                       ↓
              AsyncLogger Thread
                       ↓
                  ┌────────┐
                  │  磁盘  │
                  └────────┘
```

#### 核心设计

**1. 三级缓冲结构**

- **ThreadLocalBuffer（线程本地缓冲）**: 每个业务线程独立持有一个本地缓冲区，写入日志时仅锁自己的 buffer，无全局竞争。使用 `std::mutex` 保护，通过 RAII 自动管理生命周期。

- **Logger Buffer（中间聚合缓冲）**: 全局共享的中间缓冲区，由 Timer Thread 定时（200ms）批量收集所有线程本地缓冲的日志。使用 `std::mutex` 保护，通过 `swap` 操作减少锁持有时间。

- **AsyncLogger Buffer（异步写入队列）**: 异步写入线程持有的队列缓冲，使用 `std::queue<std::vector<std::string>>` 存储批量日志。通过 `std::condition_variable` 实现生产者-消费者模式，避免忙等待。

**2. 数据流转过程**

业务线程调用日志宏（如 `INFOLOG`）时，日志消息首先写入线程本地缓冲区，这一过程非常快速，仅需获取本线程的 mutex。Timer Thread 以 200ms 为周期定时轮询所有已注册的线程本地缓冲区，批量收集日志并聚合到 Logger Buffer 中。随后，Timer Thread 将 Logger Buffer 的内容通过 `swap` 操作转移到 AsyncLogger Buffer 的队列中，并通过条件变量通知 AsyncLogger Thread。AsyncLogger Thread 被唤醒后，从队列中取出一批日志，执行真正的磁盘 I/O 写入操作，这一过程完全异步，不阻塞任何业务线程。

**3. 并发安全和性能**

所有锁只保护轻量级操作。业务线程写入时锁仅保护 `push_back`，Timer Thread 收集日志时锁仅保护 `swap`（O(1) 指针交换），确保锁持有时间极短，降低竞争概率。

线程注册表通过原子变量 `cache_is_changed_` 标记是否需要更新。该标志仅在持有 `register_threads_mutex_` 时设为 true（线程注册/注销时），并在重建缓存后设为 false，不存在 ABA 问题。Timer Thread 轮询时先无锁读取该标志，仅在为 true 时才获取锁重建缓存，避免频繁加锁。

### 服务缓存

服务缓存模块基于 etcd 实现服务发现，采用**分段锁 + 自适应自旋锁**的并发控制策略，降低锁粒度，提升高并发场景下的查询性能。

#### 整体架构

```
                    服务发现请求
                         ↓
                  nameToIndex(hash)
                         ↓
         ┌───────────────┼───────────────┐
         ↓               ↓               ↓
    ┌────────┐      ┌────────┐      ┌────────┐
    │Bucket 0│      │Bucket 1│  ... │Bucket 7│
    │  Map   │      │  Map   │      │  Map   │
    └────────┘      └────────┘      └────────┘
    │ Lock 0 │      │ Lock 1 │      │ Lock 7 │
    └────────┘      └────────┘      └────────┘
   (SpinLock)      (SpinLock)      (SpinLock)
         ↑               ↑               ↑
         └───────────────┼───────────────┘
                         ↓
                  etcd Watcher
              (监听服务变化，失效缓存)
```

#### 核心设计

**1. 分段锁降低锁粒度**

服务缓存使用 8 个独立的哈希桶（`all_service_map_[8]`），每个桶有独立的自旋锁（`bucket_lock_[8]`）。查询服务时，先通过 `nameToIndex` 对服务名哈希取模计算桶索引，然后只锁对应的桶，不同服务名大概率落入不同桶中，多线程并发查询时锁竞争大幅降低。

**2. 自适应自旋锁优化延迟**

考虑到服务变化不频繁、缓存命中率高，查询操作（哈希表查找）和更新操作（删除缓存项）都非常快速（微秒级）。使用自适应自旋锁代替普通互斥锁，避免了系统调用和线程上下文切换的开销。自旋锁在获取失败时通过 CPU pause 指令自旋等待，自旋 100 次后自动让出 CPU，在低竞争场景下延迟极低。

**3. 被动失效策略**

etcd Watcher 监听服务变化（删除/过期），收到事件后解析服务名、计算桶索引、加锁删除对应缓存项。下次查询该服务时缓存未命中，触发 `loadByKey` 从 etcd 重新加载最新的服务列表并更新缓存。这种被动失效机制避免了主动轮询 etcd，减少网络开销。



## 压测
服务端4 io线程， 客户端2线程，8000协程，60s持续不断发起请求。  每次请求都是一个TCP连接，需要内核允许复用time_wait连接。 QPS为7400。
```
(base) flyzz@flyzz:~/rocket/build$ ./bin/test_rpc_bench -t 60 -c 8000
LOG -- CONFIG LEVEL[INFO], FILE_NAME[test_rpc_client],FILE_PATH[log/] MAX_FILE_SIZE[1000000000 B], SYNC_INTEVAL[500 ms]
Server -- PORT[12345], IO Threads[4]
Init log level [INFO]
========== Benchmark Configuration ==========
Mode: Duration (Max Speed)
Duration: 60 seconds
Concurrency: 8000
=============================================

Thread Configuration:
  Hardware Threads: 8
  Benchmark Threads: 2
  Coroutines per Thread: 4000

Benchmark started with 2 threads...

========== Benchmark Results ==========
Duration: 61 seconds
Total Requests: 451008
Successful: 451008
Failed: 0
Success Rate: 100.00%
QPS: 7393.57
Average Latency: 576.53 ms
Latency Distribution:
  P50: 776.65 ms
  P75: 856.17 ms
  P90: 872.95 ms
  P95: 880.92 ms
  P99: 900.07 ms
  Max: 942.57 ms
=======================================
```

服务端火焰图如下：
![alt text](flame.svg)
