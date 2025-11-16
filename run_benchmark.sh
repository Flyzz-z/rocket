#!/bin/bash

echo "=========================================="
echo "RPC Benchmark Tool - Quick Test"
echo "=========================================="
echo ""

# 检查可执行文件
if [ ! -f "./build/bin/test_rpc_bench" ]; then
    echo "Error: test_rpc_bench not found. Please run 'bash build.sh' first."
    exit 1
fi

if [ ! -f "./build/bin/test_rpc_server" ]; then
    echo "Error: test_rpc_server not found. Please run 'bash build.sh' first."
    exit 1
fi

# 检查etcd
if ! pgrep -x "etcd" > /dev/null; then
    echo "Error: etcd is not running."
    echo "Please start etcd first:"
    echo "  etcd --listen-client-urls http://127.0.0.1:2379 --advertise-client-urls http://127.0.0.1:2379"
    exit 1
fi

echo "Starting RPC server..."
./build/bin/test_rpc_server ./conf/rocket.xml > /tmp/rpc_server.log 2>&1 &
SERVER_PID=$!
echo "Server PID: $SERVER_PID"

# 等待服务启动和注册
echo "Waiting for service registration..."
sleep 3

echo ""
echo "=========================================="
echo "Test 1: Small Load (1000 requests, 10 workers)"
echo "=========================================="
./build/bin/test_rpc_bench -n 1000 -c 10

echo ""
echo "=========================================="
echo "Test 2: Medium Load (5000 requests, 50 workers)"
echo "=========================================="
./build/bin/test_rpc_bench -n 5000 -c 50

echo ""
echo "=========================================="
echo "Test 3: QPS Control (100 QPS for 10 seconds)"
echo "=========================================="
./build/bin/test_rpc_bench -q 100 -t 10

echo ""
echo "=========================================="
echo "Test 4: Duration Mode (20 workers for 10 seconds)"
echo "=========================================="
./build/bin/test_rpc_bench -t 10 -c 20

# 清理
echo ""
echo "=========================================="
echo "Cleaning up..."
echo "=========================================="
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "All tests completed!"
echo "Server log saved to: /tmp/rpc_server.log"
