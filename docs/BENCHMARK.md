# RPC 压力测试工具使用指南

## 概述

`test_rpc_bench` 是一个功能强大的 RPC 压力测试工具,支持多种测试模式和详细的性能统计。

## 功能特性

- **多种测试模式**: 支持总请求数、QPS控制、持续时间三种测试模式
- **并发测试**: 支持可配置的并发worker数量
- **详细统计**: 提供延迟分布(P50/P75/P90/P95/P99)、QPS、成功率等指标
- **异步协程**: 基于C++20协程实现高性能异步IO

## 使用方法

### 模式 1: 总请求数模式

发送固定数量的请求,使用指定的并发数。

```bash
./build/bin/test_rpc_bench -n <total_requests> -c <concurrency>
```

**示例**:
```bash
# 发送10000个请求,使用100个并发worker
./build/bin/test_rpc_bench -n 10000 -c 100

# 发送1000个请求,使用10个并发worker
./build/bin/test_rpc_bench -n 1000 -c 10
```

**适用场景**:
- 需要测试固定负载下的系统表现
- 比较不同并发数对性能的影响

---

### 模式 2: QPS控制模式

以指定的QPS速率发送请求,持续指定时间。

```bash
./build/bin/test_rpc_bench -q <qps> -t <duration_sec>
```

**示例**:
```bash
# 以1000 QPS的速率压测60秒
./build/bin/test_rpc_bench -q 1000 -t 60

# 以500 QPS的速率压测30秒
./build/bin/test_rpc_bench -q 500 -t 30
```

**适用场景**:
- 测试系统在特定QPS下的稳定性
- 寻找系统的QPS瓶颈(逐步提高QPS)
- 模拟真实流量场景

**注意**: 系统会自动设置合理的并发数(最多100个worker)

---

### 模式 3: 持续时间模式 (最大速度)

使用指定并发数以最大速度发送请求,持续指定时间。

```bash
./build/bin/test_rpc_bench -t <duration_sec> -c <concurrency>
```

**示例**:
```bash
# 使用50个并发worker,全速压测30秒
./build/bin/test_rpc_bench -t 30 -c 50

# 使用200个并发worker,全速压测60秒
./build/bin/test_rpc_bench -t 60 -c 200
```

**适用场景**:
- 测试系统的极限QPS
- 压力测试找出系统瓶颈
- 稳定性测试

---

## 输出指标说明

### 基础指标

- **Duration**: 实际测试持续时间(秒)
- **Total Requests**: 总请求数
- **Successful**: 成功的请求数
- **Failed**: 失败的请求数
- **Success Rate**: 成功率(%)
- **QPS**: 每秒请求数 (Queries Per Second)
- **Average Latency**: 平均延迟(毫秒)

### 延迟分布 (Percentiles)

- **P50**: 50%的请求延迟小于此值(中位数)
- **P75**: 75%的请求延迟小于此值
- **P90**: 90%的请求延迟小于此值
- **P95**: 95%的请求延迟小于此值
- **P99**: 99%的请求延迟小于此值
- **Max**: 最大延迟

### 示例输出

```
========== Benchmark Configuration ==========
Mode: Total Requests
Total Requests: 10000
Concurrency: 100
=============================================

Benchmark started...

========== Benchmark Results ==========
Duration: 12 seconds
Total Requests: 10000
Successful: 9987
Failed: 13
Success Rate: 99.87%
QPS: 832.25
Average Latency: 15.32 ms
Latency Distribution:
  P50: 12.45 ms
  P75: 18.23 ms
  P90: 25.67 ms
  P95: 32.11 ms
  P99: 48.92 ms
  Max: 87.34 ms
=======================================
```

---

## 前置条件

1. **启动 etcd 服务**:
   ```bash
   etcd --listen-client-urls http://127.0.0.1:2379 \
        --advertise-client-urls http://127.0.0.1:2379
   ```

2. **启动 RPC 服务端**:
   ```bash
   ./build/bin/test_rpc_server ./conf/rocket.xml
   ```

3. **确保服务注册成功**: 检查日志确认服务已注册到 etcd

---

## 最佳实践

### 1. 逐步提高负载

从小负载开始,逐步增加:

```bash
# 第一步: 小负载测试
./build/bin/test_rpc_bench -n 1000 -c 10

# 第二步: 中等负载
./build/bin/test_rpc_bench -n 5000 -c 50

# 第三步: 高负载
./build/bin/test_rpc_bench -n 10000 -c 100

# 第四步: 极限测试
./build/bin/test_rpc_bench -t 60 -c 200
```

### 2. QPS探测

找出系统的QPS上限:

```bash
# 从低QPS开始
./build/bin/test_rpc_bench -q 100 -t 30
./build/bin/test_rpc_bench -q 500 -t 30
./build/bin/test_rpc_bench -q 1000 -t 30
./build/bin/test_rpc_bench -q 2000 -t 30
# ... 直到成功率下降或延迟飙升
```

### 3. 稳定性测试

长时间运行测试系统稳定性:

```bash
# 中等QPS下运行5分钟
./build/bin/test_rpc_bench -q 500 -t 300

# 或者以固定并发跑10分钟
./build/bin/test_rpc_bench -t 600 -c 50
```

### 4. 性能调优

测试不同参数对性能的影响:

```bash
# 测试不同IO线程数 (修改 conf/rocket.xml 中的 io_threads)
# io_threads=4
./build/bin/test_rpc_bench -n 10000 -c 100

# io_threads=8
./build/bin/test_rpc_bench -n 10000 -c 100

# io_threads=16
./build/bin/test_rpc_bench -n 10000 -c 100
```

---

## 常见问题

### Q: 为什么QPS上不去?

**可能原因**:
1. 并发数太低 → 增加 `-c` 参数
2. 服务端IO线程不足 → 调整 `rocket.xml` 中的 `io_threads`
3. 网络瓶颈 → 检查网络配置
4. 服务端处理逻辑慢 → 优化业务代码

### Q: 延迟突然飙升?

**排查步骤**:
1. 检查服务端负载 (`top`, `htop`)
2. 查看网络状况 (`netstat`, `ss`)
3. 检查是否有GC或内存问题
4. 查看服务端日志

### Q: 成功率低于预期?

**可能原因**:
1. 超时设置太短 → 检查 `controller->SetTimeout()`
2. 服务端过载 → 降低QPS或并发数
3. 网络不稳定 → 检查网络连接
4. 服务端错误 → 查看服务端日志

---

## 性能调优建议

### 客户端调优

1. **增加并发数**: 对于IO密集型测试,提高并发数可以提升QPS
2. **调整超时**: 根据实际网络延迟调整超时时间
3. **禁用日志**: 测试时可以减少日志输出

### 服务端调优

1. **IO线程数**: 根据CPU核心数调整(建议设置为 CPU核心数 * 1.5 - 2)
2. **连接池**: 优化数据库连接池大小
3. **业务逻辑**: 优化RPC处理逻辑,减少阻塞操作

---

## 参数总结

| 参数 | 说明 | 示例 |
|------|------|------|
| `-n` | 总请求数 | `-n 10000` |
| `-c` | 并发worker数 | `-c 100` |
| `-q` | 目标QPS | `-q 1000` |
| `-t` | 持续时间(秒) | `-t 60` |

**有效组合**:
- `-n` + `-c`: 模式1 (总请求数)
- `-q` + `-t`: 模式2 (QPS控制)
- `-t` + `-c`: 模式3 (持续时间,最大速度)
