#include <iostream>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>
#include <iomanip>
#include <memory>
#include "rocket/common/config.h"
#include "rocket/logger/log.h"
#include "rocket/net/event_loop.h"
#include "rocket/net/rpc/etcd_registry.h"
#include "rocket/net/rpc/rpc_channel.h"
#include "proto/co_stub/co_order_stub.h"
#include "rpc_controller.h"

// 线程本地统计结构
struct ThreadLocalStats {
  int64_t total_requests = 0;
  int64_t successful_requests = 0;
  int64_t failed_requests = 0;
  int64_t total_latency_us = 0;
  std::vector<int64_t> latencies;

  // 预分配空间避免频繁重新分配
  ThreadLocalStats() {
    latencies.reserve(100000);  // 预分配10万个延迟记录空间
  }
};

// 性能统计类 - 无锁设计
class BenchmarkStats {
public:
  void recordRequest(int64_t latency_us, bool success) {
    // 获取线程本地统计，使用 shared_ptr 管理生命周期
    thread_local std::shared_ptr<ThreadLocalStats> local_stats;
    thread_local bool registered = false;

    // 第一次使用时创建并注册到全局列表
    if (!registered) {
      local_stats = std::make_shared<ThreadLocalStats>();
      std::lock_guard<std::mutex> lock(stats_mutex_);
      thread_stats_.push_back(local_stats);
      registered = true;
    }

    // 无锁更新线程本地统计
    local_stats->total_requests++;
    if (success) {
      local_stats->successful_requests++;
      local_stats->total_latency_us += latency_us;
      local_stats->latencies.push_back(latency_us);
    } else {
      local_stats->failed_requests++;
    }
  }

  void printStats(int duration_sec) {
    // 汇总所有线程的统计
    int64_t total_requests = 0;
    int64_t successful_requests = 0;
    int64_t failed_requests = 0;
    int64_t total_latency_us = 0;
    std::vector<int64_t> all_latencies;

    {
      std::lock_guard<std::mutex> lock(stats_mutex_);
      // 预估总延迟数量
      size_t total_count = 0;
      for (auto& stats : thread_stats_) {
        total_count += stats->latencies.size();
      }
      all_latencies.reserve(total_count);

      // 汇总所有线程的数据
      for (auto& stats : thread_stats_) {
        total_requests += stats->total_requests;
        successful_requests += stats->successful_requests;
        failed_requests += stats->failed_requests;
        total_latency_us += stats->total_latency_us;
        all_latencies.insert(all_latencies.end(),
                            stats->latencies.begin(),
                            stats->latencies.end());
      }
    }

    std::cout << "\n========== Benchmark Results ==========\n";
    std::cout << "Duration: " << duration_sec << " seconds\n";
    std::cout << "Total Requests: " << total_requests << "\n";
    std::cout << "Successful: " << successful_requests << "\n";
    std::cout << "Failed: " << failed_requests << "\n";

    double success_rate = (total_requests > 0)
        ? (100.0 * successful_requests / total_requests)
        : 0.0;
    std::cout << "Success Rate: " << std::fixed << std::setprecision(2)
              << success_rate << "%\n";

    double qps = (duration_sec > 0)
        ? (1.0 * successful_requests / duration_sec)
        : 0.0;
    std::cout << "QPS: " << std::fixed << std::setprecision(2) << qps << "\n";

    if (successful_requests > 0) {
      double avg_latency = 1.0 * total_latency_us / successful_requests;
      std::cout << "Average Latency: " << std::fixed << std::setprecision(2)
                << avg_latency / 1000.0 << " ms\n";

      // 计算延迟百分位
      if (!all_latencies.empty()) {
        std::sort(all_latencies.begin(), all_latencies.end());
        size_t count = all_latencies.size();
        std::cout << "Latency Distribution:\n";
        std::cout << "  P50: " << all_latencies[count * 50 / 100] / 1000.0 << " ms\n";
        std::cout << "  P75: " << all_latencies[count * 75 / 100] / 1000.0 << " ms\n";
        std::cout << "  P90: " << all_latencies[count * 90 / 100] / 1000.0 << " ms\n";
        std::cout << "  P95: " << all_latencies[count * 95 / 100] / 1000.0 << " ms\n";
        std::cout << "  P99: " << all_latencies[count * 99 / 100] / 1000.0 << " ms\n";
        std::cout << "  Max: " << all_latencies[count - 1] / 1000.0 << " ms\n";
      }
    }
    std::cout << "=======================================\n";
  }

  void reset() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    for (auto& stats : thread_stats_) {
      stats->total_requests = 0;
      stats->successful_requests = 0;
      stats->failed_requests = 0;
      stats->total_latency_us = 0;
      stats->latencies.clear();
    }
    thread_stats_.clear();
  }

private:
  std::mutex stats_mutex_;
  std::vector<std::shared_ptr<ThreadLocalStats>> thread_stats_;
};

// 全局统计对象
BenchmarkStats g_stats;
std::atomic<bool> g_running{true};

// 单个请求协程
asio::awaitable<void> sendSingleRequest(int req_id) {
  auto start = std::chrono::high_resolution_clock::now();
  bool success = false;

  try {
    NEWRPCCHANNEL("Order", channel);
    NEWMESSAGE(makeOrderRequest, request);
    NEWMESSAGE(makeOrderResponse, response);

    request->set_price(100 + req_id % 100);
    request->set_goods("item_" + std::to_string(req_id));

    NEWRPCCONTROLLER(controller);
    controller->SetMsgId(std::to_string(req_id));
    controller->SetTimeout(5000);

    channel->Init(controller, request, response, nullptr);
    co_await CoOrderStub(channel.get())
        .coMakeOrder(controller.get(), request.get(), response.get(), nullptr);

    if (!controller->Failed()) {
      success = true;
    }
  } catch (const std::exception& e) {
    // 请求失败
  }

  auto end = std::chrono::high_resolution_clock::now();
  auto latency_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

  g_stats.recordRequest(latency_us, success);
}

// 持续发送请求的协程 - 每个协程独立循环，实现真正的并发
asio::awaitable<void> continuousBenchmark(int worker_id) {
  int req_count = 0;
  while (g_running.load()) {
    int req_id = worker_id * 1000000 + req_count++;
    co_await sendSingleRequest(req_id);
  }
}

// 发送固定数量请求的协程
asio::awaitable<void> fixedRequestsBenchmark(int worker_id, int num_requests) {
  for (int i = 0; i < num_requests && g_running.load(); ++i) {
    int req_id = worker_id * 1000000 + i;
    co_await sendSingleRequest(req_id);
  }
}

// 按QPS发送请求的协程
asio::awaitable<void> qpsBenchmark(int worker_id, int target_qps, int duration_sec) {
  auto start_time = std::chrono::steady_clock::now();
  int64_t interval_us = 1000000 / target_qps; // 每个请求的间隔(微秒)
  int req_count = 0;

  while (g_running.load()) {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

    if (elapsed_sec >= duration_sec) {
      break;
    }

    int req_id = worker_id * 100000 + req_count++;
    co_await sendSingleRequest(req_id);

    // 控制请求速率
    if (interval_us > 1000) {
      auto sleep_time = std::chrono::microseconds(interval_us);
      co_await asio::steady_timer(co_await asio::this_coro::executor, sleep_time).async_wait(asio::use_awaitable);
    }
  }
}

void printUsage(const char* program) {
  std::cout << "Usage: " << program << " [mode] [options]\n";
  std::cout << "\nModes:\n";
  std::cout << "  1. Total requests mode:\n";
  std::cout << "     " << program << " -n <total_requests> -c <concurrency>\n";
  std::cout << "     Example: " << program << " -n 10000 -c 100\n";
  std::cout << "            Send 10000 requests with 100 concurrent workers\n\n";
  std::cout << "  2. QPS mode:\n";
  std::cout << "     " << program << " -q <qps> -t <duration_sec>\n";
  std::cout << "     Example: " << program << " -q 1000 -t 60\n";
  std::cout << "            Send requests at 1000 QPS for 60 seconds\n\n";
  std::cout << "  3. Duration mode:\n";
  std::cout << "     " << program << " -t <duration_sec> -c <concurrency>\n";
  std::cout << "     Example: " << program << " -t 30 -c 50\n";
  std::cout << "            Run 50 workers for 30 seconds (max speed)\n";
}

int main(int argc, char* argv[]) {
  // 解析命令行参数
  int total_requests = 0;
  int concurrency = 1;
  int target_qps = 0;
  int duration_sec = 0;

  for (int i = 1; i < argc; i += 2) {
    if (i + 1 >= argc) {
      printUsage(argv[0]);
      return 1;
    }

    std::string arg = argv[i];
    int value = std::atoi(argv[i + 1]);

    if (arg == "-n") {
      total_requests = value;
    } else if (arg == "-c") {
      concurrency = value;
    } else if (arg == "-q") {
      target_qps = value;
    } else if (arg == "-t") {
      duration_sec = value;
    } else {
      std::cout << "Unknown argument: " << arg << "\n";
      printUsage(argv[0]);
      return 1;
    }
  }

  // 验证参数组合
  bool mode_total = (total_requests > 0 && concurrency > 0);
  bool mode_qps = (target_qps > 0 && duration_sec > 0);
  bool mode_duration = (duration_sec > 0 && concurrency > 0 && target_qps == 0);

  if (!mode_total && !mode_qps && !mode_duration) {
    std::cout << "Error: Invalid parameter combination\n\n";
    printUsage(argv[0]);
    return 1;
  }

  // 初始化
  rocket::Config::SetGlobalConfig("../conf/rocket_client.xml");
  rocket::Logger::InitGlobalLogger();
  rocket::EtcdRegistry::initAsClient("127.0.0.1", 2379, "root", "123456");

  std::cout << "========== Benchmark Configuration ==========\n";
  if (mode_total) {
    std::cout << "Mode: Total Requests\n";
    std::cout << "Total Requests: " << total_requests << "\n";
    std::cout << "Concurrency: " << concurrency << "\n";
  } else if (mode_qps) {
    std::cout << "Mode: QPS Control\n";
    std::cout << "Target QPS: " << target_qps << "\n";
    std::cout << "Duration: " << duration_sec << " seconds\n";
    concurrency = std::min(target_qps, 100); // 自动设置并发数
    std::cout << "Concurrency: " << concurrency << " workers\n";
  } else if (mode_duration) {
    std::cout << "Mode: Duration (Max Speed)\n";
    std::cout << "Duration: " << duration_sec << " seconds\n";
    std::cout << "Concurrency: " << concurrency << "\n";
  }
  std::cout << "=============================================\n\n";

  // 计算线程和协程分配
  int num_threads = 2;
  if (num_threads == 0) num_threads = 2;  // 默认2线程
  if (num_threads > concurrency) num_threads = concurrency;  // 线程数不超过并发数

  int coroutines_per_thread = concurrency / num_threads;
  int remaining_coroutines = concurrency % num_threads;

  std::cout << "Thread Configuration:\n";
  std::cout << "  Hardware Threads: " << std::thread::hardware_concurrency() << "\n";
  std::cout << "  Benchmark Threads: " << num_threads << "\n";
  std::cout << "  Coroutines per Thread: " << coroutines_per_thread;
  if (remaining_coroutines > 0) {
    std::cout << " (+" << remaining_coroutines << " on first thread)";
  }
  std::cout << "\n\n";

  auto start_time = std::chrono::steady_clock::now();

  // 创建工作线程
  std::vector<std::thread> threads;
  std::atomic<int> ready_threads{0};

  for (int thread_id = 0; thread_id < num_threads; ++thread_id) {
    threads.emplace_back([thread_id, num_threads, coroutines_per_thread, remaining_coroutines,
                          mode_total, mode_qps, mode_duration,
                          total_requests, target_qps, duration_sec, &ready_threads]() {
      // 每个线程创建自己的事件循环
      rocket::EventLoop* event_loop = rocket::EventLoop::getThreadEventLoop();

      // 计算这个线程的协程数
      int num_coroutines = coroutines_per_thread;
      if (thread_id == 0) {
        num_coroutines += remaining_coroutines;  // 第一个线程处理剩余的协程
      }

      // 启动压测协程
      if (mode_total) {
        // 模式1: 总请求数模式 - 每个协程独立发送固定数量请求
        int total_workers = num_threads * coroutines_per_thread + remaining_coroutines;
        int requests_per_worker = total_requests / total_workers;
        for (int i = 0; i < num_coroutines; ++i) {
          int worker_id = thread_id * coroutines_per_thread + i;
          event_loop->addCoroutine([worker_id, requests_per_worker]() -> asio::awaitable<void> {
            co_await fixedRequestsBenchmark(worker_id, requests_per_worker);
          });
        }
      } else if (mode_qps) {
        // 模式2: QPS控制模式
        int total_workers = num_threads * coroutines_per_thread + remaining_coroutines;
        int qps_per_worker = target_qps / total_workers;
        for (int i = 0; i < num_coroutines; ++i) {
          int worker_id = thread_id * coroutines_per_thread + i;
          event_loop->addCoroutine([worker_id, qps_per_worker, duration_sec]() -> asio::awaitable<void> {
            co_await qpsBenchmark(worker_id, qps_per_worker, duration_sec);
          });
        }
      } else if (mode_duration) {
        // 模式3: 持续时间模式(最大速度) - 每个协程独立循环，真正并发
        for (int i = 0; i < num_coroutines; ++i) {
          int worker_id = thread_id * coroutines_per_thread + i;
          event_loop->addCoroutine([worker_id]() -> asio::awaitable<void> {
            co_await continuousBenchmark(worker_id);
          });
        }

        // 每个线程都设置定时器来停止自己的事件循环
        event_loop->addTimer(duration_sec * 1000, false, [event_loop]() {
          g_running.store(false);
          event_loop->stop();  // 停止当前线程的事件循环
        });
      }

      // 标记线程就绪
      ready_threads.fetch_add(1);

      // 运行事件循环
      event_loop->run();
    });
  }

  // 等待所有线程启动
  while (ready_threads.load() < num_threads) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::cout << "Benchmark started with " << num_threads << " threads...\n";

  // 等待所有线程完成
  for (auto& thread : threads) {
    thread.join();
  }

  auto end_time = std::chrono::steady_clock::now();
  int actual_duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();

  // 等待一下确保所有请求完成
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // 打印统计信息
  g_stats.printStats(actual_duration > 0 ? actual_duration : 1);



  return 0;
}
