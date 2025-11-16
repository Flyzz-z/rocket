#include <iostream>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <algorithm>
#include <iomanip>
#include <cmath>
#include "rocket/common/config.h"
#include "rocket/common/log.h"
#include "rocket/net/event_loop.h"
#include "rocket/net/rpc/etcd_registry.h"
#include "rocket/net/rpc/rpc_channel.h"
#include "proto/co_stub/co_order_stub.h"
#include "rpc_controller.h"

// 性能统计类
class BenchmarkStats {
public:
  void recordRequest(int64_t latency_us, bool success) {
    total_requests_.fetch_add(1);
    if (success) {
      successful_requests_.fetch_add(1);
      total_latency_us_.fetch_add(latency_us);

      // 记录延迟分布
      latencies_mutex_.lock();
      latencies_.push_back(latency_us);
      latencies_mutex_.unlock();
    } else {
      failed_requests_.fetch_add(1);
    }
  }

  void printStats(int duration_sec) {
    std::cout << "\n========== Benchmark Results ==========\n";
    std::cout << "Duration: " << duration_sec << " seconds\n";
    std::cout << "Total Requests: " << total_requests_.load() << "\n";
    std::cout << "Successful: " << successful_requests_.load() << "\n";
    std::cout << "Failed: " << failed_requests_.load() << "\n";

    double success_rate = (total_requests_.load() > 0)
        ? (100.0 * successful_requests_.load() / total_requests_.load())
        : 0.0;
    std::cout << "Success Rate: " << std::fixed << std::setprecision(2)
              << success_rate << "%\n";

    double qps = (duration_sec > 0)
        ? (1.0 * successful_requests_.load() / duration_sec)
        : 0.0;
    std::cout << "QPS: " << std::fixed << std::setprecision(2) << qps << "\n";

    if (successful_requests_.load() > 0) {
      double avg_latency = 1.0 * total_latency_us_.load() / successful_requests_.load();
      std::cout << "Average Latency: " << std::fixed << std::setprecision(2)
                << avg_latency / 1000.0 << " ms\n";

      // 计算延迟百分位
      latencies_mutex_.lock();
      std::sort(latencies_.begin(), latencies_.end());
      size_t count = latencies_.size();
      if (count > 0) {
        std::cout << "Latency Distribution:\n";
        std::cout << "  P50: " << latencies_[count * 50 / 100] / 1000.0 << " ms\n";
        std::cout << "  P75: " << latencies_[count * 75 / 100] / 1000.0 << " ms\n";
        std::cout << "  P90: " << latencies_[count * 90 / 100] / 1000.0 << " ms\n";
        std::cout << "  P95: " << latencies_[count * 95 / 100] / 1000.0 << " ms\n";
        std::cout << "  P99: " << latencies_[count * 99 / 100] / 1000.0 << " ms\n";
        std::cout << "  Max: " << latencies_[count - 1] / 1000.0 << " ms\n";
      }
      latencies_mutex_.unlock();
    }
    std::cout << "=======================================\n";
  }

  void reset() {
    total_requests_.store(0);
    successful_requests_.store(0);
    failed_requests_.store(0);
    total_latency_us_.store(0);
    latencies_mutex_.lock();
    latencies_.clear();
    latencies_mutex_.unlock();
  }

private:
  std::atomic<int64_t> total_requests_{0};
  std::atomic<int64_t> successful_requests_{0};
  std::atomic<int64_t> failed_requests_{0};
  std::atomic<int64_t> total_latency_us_{0};

  std::mutex latencies_mutex_;
  std::vector<int64_t> latencies_;
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

// 持续发送请求的协程
asio::awaitable<void> continuousBenchmark(int worker_id, int requests_per_worker) {
  for (int i = 0; i < requests_per_worker && g_running.load(); ++i) {
    int req_id = worker_id * 10000 + i;
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
  rocket::Config::SetGlobalConfig(NULL);
  rocket::Logger::InitGlobalLogger(1);
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

  // 创建事件循环
  rocket::EventLoop* event_loop = rocket::EventLoop::getThreadEventLoop();

  auto start_time = std::chrono::steady_clock::now();

  // 启动压测协程
  if (mode_total) {
    // 模式1: 总请求数模式
    int requests_per_worker = total_requests / concurrency;
    for (int i = 0; i < concurrency; ++i) {
      event_loop->addCoroutine([i, requests_per_worker]() -> asio::awaitable<void> {
        co_await continuousBenchmark(i, requests_per_worker);
      });
    }
  } else if (mode_qps) {
    // 模式2: QPS控制模式
    int qps_per_worker = target_qps / concurrency;
    for (int i = 0; i < concurrency; ++i) {
      event_loop->addCoroutine([i, qps_per_worker, duration_sec]() -> asio::awaitable<void> {
        co_await qpsBenchmark(i, qps_per_worker, duration_sec);
      });
    }
  } else if (mode_duration) {
    // 模式3: 持续时间模式(最大速度)
    for (int i = 0; i < concurrency; ++i) {
      event_loop->addCoroutine([i]() -> asio::awaitable<void> {
        co_await continuousBenchmark(i, 1000000); // 足够大的数字
      });
    }

    // 启动定时器停止测试
    event_loop->addTimer(duration_sec * 1000, false, [&]() {
      g_running.store(false);
    });
  }

  std::cout << "Benchmark started...\n";

  // 运行事件循环
  event_loop->run();

  auto end_time = std::chrono::steady_clock::now();
  int actual_duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time).count();

  // 等待一下确保所有请求完成
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // 打印统计信息
  g_stats.printStats(actual_duration > 0 ? actual_duration : 1);

  // 清理
  rocket::EtcdRegistry::GetInstance()->stopWatcher();
  rocket::Logger::GetGlobalLogger()->flush();

  return 0;
}
