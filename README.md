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
