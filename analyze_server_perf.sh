#!/bin/bash
# 文件名: analyze_server_perf.sh
# 用途: 在运行压测时分析服务端性能瓶颈

set -e

# 配置参数
PERF_DURATION=35        # perf记录时长（秒）
BENCH_DURATION=30       # 压测时长（秒）
BENCH_CONCURRENCY=4000   # 并发数
PERF_FREQ=99            # CPU采样频率
BUILD_DIR="/home/flyzz/rocket/build"

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}🎯 RPC Server Performance Analysis Tool${NC}"
echo "=========================================="

# 检查服务端是否运行
SERVER_PID=$(pgrep -f test_rpc_server | head -1)
if [ -z "$SERVER_PID" ]; then
    echo -e "${RED}❌ Error: test_rpc_server not running${NC}"
    echo "Please start the server first:"
    echo "  $BUILD_DIR/bin/test_rpc_server ../conf/rocket.xml"
    exit 1
fi

echo -e "${GREEN}✓ Server found${NC}"
echo "  PID: $SERVER_PID"
echo ""

# 检查perf是否可用
if ! command -v perf &> /dev/null; then
    echo -e "${RED}❌ Error: perf not installed${NC}"
    echo "Install with: sudo apt install linux-tools-generic"
    exit 1
fi

# 创建输出目录
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="/tmp/perf_analysis_${TIMESTAMP}"
mkdir -p "$OUTPUT_DIR"
cd "$OUTPUT_DIR"

echo -e "${YELLOW}📁 Output directory: $OUTPUT_DIR${NC}"
echo ""

# 记录系统信息
echo "System Information:" > system_info.txt
echo "==================" >> system_info.txt
echo "Date: $(date)" >> system_info.txt
echo "Kernel: $(uname -r)" >> system_info.txt
echo "CPU Cores: $(nproc)" >> system_info.txt
echo "Server PID: $SERVER_PID" >> system_info.txt
echo "Perf Duration: ${PERF_DURATION}s" >> system_info.txt
echo "Bench Duration: ${BENCH_DURATION}s" >> system_info.txt
echo "Bench Concurrency: $BENCH_CONCURRENCY" >> system_info.txt
echo "" >> system_info.txt

# 记录网络参数
echo "Network Parameters:" >> system_info.txt
sysctl net.core.somaxconn >> system_info.txt
sysctl net.ipv4.tcp_max_syn_backlog >> system_info.txt
sysctl net.ipv4.ip_local_port_range >> system_info.txt
sysctl net.ipv4.tcp_tw_reuse >> system_info.txt

echo -e "${BLUE}📊 Step 1: Starting perf recording (${PERF_DURATION}s)...${NC}"
perf record -p $SERVER_PID -g -F $PERF_FREQ -o server.perf.data -- sleep $PERF_DURATION &
PERF_PID=$!
echo "  Perf PID: $PERF_PID"

# 等待perf启动
sleep 2

echo -e "${BLUE}🚀 Step 2: Running benchmark (${BENCH_DURATION}s, concurrency=$BENCH_CONCURRENCY)...${NC}"
$BUILD_DIR/bin/test_rpc_bench -t $BENCH_DURATION -c $BENCH_CONCURRENCY > bench_result.txt 2>&1
echo -e "${GREEN}✓ Benchmark completed${NC}"

# 显示简要结果
echo ""
echo "Benchmark Results:"
grep -E "QPS|Average Latency|Success Rate|Failed:" bench_result.txt || true
echo ""

echo -e "${BLUE}⏳ Step 3: Waiting for perf to finish...${NC}"
wait $PERF_PID
echo -e "${GREEN}✓ Perf recording completed${NC}"

echo -e "${BLUE}📈 Step 4: Generating performance reports...${NC}"

# 添加二进制文件到buildid缓存
echo "Adding binary to perf buildid cache..."
perf buildid-cache --add $BUILD_DIR/bin/test_rpc_server 2>/dev/null || true

# 生成完整报告
echo "Generating full report..."
perf report -i server.perf.data --stdio --symfs=/ --kallsyms=/proc/kallsyms > perf_full_report.txt 2>&1

# 提取热点函数（前30个）
echo "Extracting top hotspots..."
echo "=== Top 30 CPU Hotspots ===" > perf_hotspots.txt
echo "Overhead  Symbol" >> perf_hotspots.txt
echo "--------  ------" >> perf_hotspots.txt
perf report -i server.perf.data --stdio --sort symbol --symfs=/ --kallsyms=/proc/kallsyms 2>/dev/null | \
    grep -E "^\s+[0-9]" | head -30 >> perf_hotspots.txt

# 按共享库分析
echo "Analyzing by shared object..."
echo "=== CPU Usage by Library ===" > perf_by_library.txt
perf report -i server.perf.data --stdio --sort dso --symfs=/ --kallsyms=/proc/kallsyms 2>/dev/null | \
    grep -E "^\s+[0-9]" | head -20 >> perf_by_library.txt

# 查找特定瓶颈
echo "Identifying potential bottlenecks..."
echo "=== Potential Performance Bottlenecks ===" > perf_bottlenecks.txt
echo "" >> perf_bottlenecks.txt

echo "--- Logging Related ---" >> perf_bottlenecks.txt
perf report -i server.perf.data --stdio --symfs=/ --kallsyms=/proc/kallsyms 2>/dev/null | \
    grep -iE "log|Logger|printf|write.*log" | head -10 >> perf_bottlenecks.txt
echo "" >> perf_bottlenecks.txt

echo "--- Lock Contention ---" >> perf_bottlenecks.txt
perf report -i server.perf.data --stdio --symfs=/ --kallsyms=/proc/kallsyms 2>/dev/null | \
    grep -iE "mutex|lock|pthread_mutex|futex" | head -10 >> perf_bottlenecks.txt
echo "" >> perf_bottlenecks.txt

echo "--- Serialization ---" >> perf_bottlenecks.txt
perf report -i server.perf.data --stdio --symfs=/ --kallsyms=/proc/kallsyms 2>/dev/null | \
    grep -iE "serial|protobuf|parse|encode|decode" | head -10 >> perf_bottlenecks.txt
echo "" >> perf_bottlenecks.txt

echo "--- Memory Operations ---" >> perf_bottlenecks.txt
perf report -i server.perf.data --stdio --symfs=/ --kallsyms=/proc/kallsyms 2>/dev/null | \
    grep -iE "malloc|free|memcpy|memset|new|delete" | head -10 >> perf_bottlenecks.txt
echo "" >> perf_bottlenecks.txt

echo "--- String Operations ---" >> perf_bottlenecks.txt
perf report -i server.perf.data --stdio --symfs=/ --kallsyms=/proc/kallsyms 2>/dev/null | \
    grep -iE "string|_M_append|_M_mutate" | head -10 >> perf_bottlenecks.txt

# 生成摘要报告
echo "=== Performance Analysis Summary ===" > analysis_summary.txt
echo "Generated: $(date)" >> analysis_summary.txt
echo "" >> analysis_summary.txt

echo "📊 Benchmark Results:" >> analysis_summary.txt
grep -E "QPS|Average Latency|Success Rate|Total Requests|Failed:|P99" bench_result.txt >> analysis_summary.txt || true
echo "" >> analysis_summary.txt

echo "🔥 Top 10 CPU Hotspots:" >> analysis_summary.txt
head -13 perf_hotspots.txt >> analysis_summary.txt
echo "" >> analysis_summary.txt

echo "📚 CPU by Library:" >> analysis_summary.txt
head -10 perf_by_library.txt >> analysis_summary.txt
echo "" >> analysis_summary.txt

echo "⚠️  Potential Issues:" >> analysis_summary.txt
echo "Check perf_bottlenecks.txt for details" >> analysis_summary.txt

echo -e "${GREEN}✅ Analysis Complete!${NC}"
echo "=========================================="
echo ""
echo -e "${YELLOW}📄 Generated Files:${NC}"
echo "  📊 analysis_summary.txt   - Quick overview"
echo "  🔥 perf_hotspots.txt      - Top 30 CPU hotspots"
echo "  📚 perf_by_library.txt    - Usage by library"
echo "  ⚠️  perf_bottlenecks.txt   - Potential issues"
echo "  📝 bench_result.txt       - Benchmark output"
echo "  📈 perf_full_report.txt   - Full perf report"
echo "  💾 server.perf.data       - Raw perf data"
echo "  ℹ️  system_info.txt        - System configuration"
echo ""
echo -e "${BLUE}🔍 Quick Analysis:${NC}"
echo ""
cat analysis_summary.txt
echo ""
echo -e "${YELLOW}📁 All results saved to: $OUTPUT_DIR${NC}"
echo ""
echo "To view full perf report interactively:"
echo "  perf report -i $OUTPUT_DIR/server.perf.data --symfs=/ --kallsyms=/proc/kallsyms"
echo ""
echo "To generate flame graph (if FlameGraph is installed):"
echo "  perf script -i $OUTPUT_DIR/server.perf.data --symfs=/ --kallsyms=/proc/kallsyms | stackcollapse-perf.pl | flamegraph.pl > flame.svg"
