# Linux 网络参数查看和设置命令手册

## 一、查看参数

### 1.1 查看单个参数
```bash
# 查看 somaxconn
sysctl net.core.somaxconn

# 查看 tcp_max_syn_backlog
sysctl net.ipv4.tcp_max_syn_backlog

# 查看端口范围
sysctl net.ipv4.ip_local_port_range

# 查看 TIME_WAIT 复用
sysctl net.ipv4.tcp_tw_reuse
```

### 1.2 批量查看相关参数
```bash
# 查看所有网络相关参数
sysctl -a | grep -E "somaxconn|syn_backlog|local_port|tw_reuse"

# 查看所有TCP参数
sysctl -a | grep tcp

# 查看所有网络核心参数
sysctl -a | grep net.core
```

### 1.3 查看参数文件位置
```bash
# 当前运行时参数（/proc虚拟文件系统）
cat /proc/sys/net/core/somaxconn
cat /proc/sys/net/ipv4/tcp_max_syn_backlog
cat /proc/sys/net/ipv4/ip_local_port_range
cat /proc/sys/net/ipv4/tcp_tw_reuse

# 持久化配置文件
cat /etc/sysctl.conf
# 或
cat /etc/sysctl.d/*.conf
```

---

## 二、设置参数

### 2.1 临时设置（重启后失效）

#### 方法1: 使用 sysctl -w
```bash
# 设置 somaxconn
sudo sysctl -w net.core.somaxconn=8192

# 设置 tcp_max_syn_backlog
sudo sysctl -w net.ipv4.tcp_max_syn_backlog=8192

# 设置端口范围
sudo sysctl -w net.ipv4.ip_local_port_range="1024 65535"

# 开启 TIME_WAIT 复用
sudo sysctl -w net.ipv4.tcp_tw_reuse=1
```

#### 方法2: 直接写入 /proc
```bash
# 设置 somaxconn
echo 8192 | sudo tee /proc/sys/net/core/somaxconn

# 设置 tcp_max_syn_backlog
echo 8192 | sudo tee /proc/sys/net/ipv4/tcp_max_syn_backlog

# 设置端口范围
echo "1024 65535" | sudo tee /proc/sys/net/ipv4/ip_local_port_range

# 开启 TIME_WAIT 复用
echo 1 | sudo tee /proc/sys/net/ipv4/tcp_tw_reuse
```

### 2.2 永久设置（重启后仍有效）

#### 修改 /etc/sysctl.conf
```bash
# 编辑配置文件
sudo vim /etc/sysctl.conf

# 添加以下内容（如果已存在则修改）
net.core.somaxconn = 8192
net.ipv4.tcp_max_syn_backlog = 8192
net.ipv4.ip_local_port_range = 1024 65535
net.ipv4.tcp_tw_reuse = 1

# 保存后应用配置
sudo sysctl -p

# 或指定配置文件
sudo sysctl -p /etc/sysctl.conf
```

#### 使用独立配置文件（推荐）
```bash
# 创建专用配置文件
sudo vim /etc/sysctl.d/99-network-tuning.conf

# 添加内容（同上）
net.core.somaxconn = 8192
net.ipv4.tcp_max_syn_backlog = 8192
net.ipv4.ip_local_port_range = 1024 65535
net.ipv4.tcp_tw_reuse = 1

# 应用配置
sudo sysctl -p /etc/sysctl.d/99-network-tuning.conf

# 或重新加载所有配置
sudo sysctl --system
```

---

## 三、验证和监控

### 3.1 验证参数是否生效
```bash
# 验证所有设置
sysctl net.core.somaxconn net.ipv4.tcp_max_syn_backlog \
       net.ipv4.ip_local_port_range net.ipv4.tcp_tw_reuse

# 预期输出:
# net.core.somaxconn = 8192
# net.ipv4.tcp_max_syn_backlog = 8192
# net.ipv4.ip_local_port_range = 1024	65535
# net.ipv4.tcp_tw_reuse = 1
```

### 3.2 查看网络连接状态
```bash
# 查看所有TCP连接统计
ss -s

# 输出示例:
# Total: 1234
# TCP:   567 (estab 123, closed 234, orphaned 0, timewait 200)

# 查看各状态连接数
ss -tan | awk '{print $1}' | sort | uniq -c | sort -rn

# 输出示例:
#    500 ESTAB
#    200 TIME-WAIT
#    100 LISTEN
#     50 SYN-SENT

# 只看TIME_WAIT数量
ss -tan | grep TIME-WAIT | wc -l
```

### 3.3 监控队列溢出情况
```bash
# 查看网络统计（包含溢出计数）
netstat -s | grep -i overflow

# 输出示例:
#   123 times the listen queue of a socket overflowed
#   ↑ accept队列溢出次数

# 查看SYN相关统计
netstat -s | grep -i syn

# 输出示例:
#   456 SYNs to LISTEN sockets dropped
#   ↑ SYN包被丢弃次数（SYN队列满）

# 实时监控（每秒刷新）
watch -n 1 'netstat -s | grep -E "overflow|dropped"'
```

### 3.4 查看端口使用情况
```bash
# 统计各端口范围的连接数
ss -tan | awk '{print $5}' | cut -d: -f2 | grep -E '^[0-9]+$' | \
  awk '{if($1>=1024 && $1<=65535) print $1}' | wc -l

# 查看本地端口使用最多的
ss -tan | awk '{print $4}' | cut -d: -f2 | sort | uniq -c | sort -rn | head -20

# 查看TIME_WAIT占用的本地端���数
ss -tan state time-wait | wc -l
```

### 3.5 监控服务端accept队列
```bash
# 查看监听socket的队列状态
ss -ltn

# 输出示例:
# State   Recv-Q Send-Q  Local Address:Port  Peer Address:Port
# LISTEN  0      128     *:12345              *:*
#         ↑      ↑
#    当前队列   最大队列(受somaxconn限制)

# 如果Recv-Q > 0，说明有积压
# 如果Recv-Q = Send-Q，说明队列满了

# 实时监控
watch -n 1 'ss -ltn sport = :12345'
```

---

## 四、快速诊断脚本

### 4.1 一键查看当前配置
```bash
#!/bin/bash
echo "=== 当前网络参数配置 ==="
echo "somaxconn: $(sysctl -n net.core.somaxconn)"
echo "tcp_max_syn_backlog: $(sysctl -n net.ipv4.tcp_max_syn_backlog)"
echo "ip_local_port_range: $(sysctl -n net.ipv4.ip_local_port_range)"
echo "tcp_tw_reuse: $(sysctl -n net.ipv4.tcp_tw_reuse)"
echo ""
echo "=== 连接状态统计 ==="
ss -s
echo ""
echo "=== TIME_WAIT 连接数 ==="
ss -tan state time-wait | wc -l
echo ""
echo "=== 队列溢出统计 ==="
netstat -s | grep -E "overflow|dropped" | head -5
```

保存为 `check_network.sh`，运行：
```bash
chmod +x check_network.sh
./check_network.sh
```

### 4.2 一键应用优化配置
```bash
#!/bin/bash
echo "正在应用网络优化参数..."

sudo sysctl -w net.core.somaxconn=8192
sudo sysctl -w net.ipv4.tcp_max_syn_backlog=8192
sudo sysctl -w net.ipv4.ip_local_port_range="1024 65535"
sudo sysctl -w net.ipv4.tcp_tw_reuse=1

echo "临时配置已应用，验证结果:"
sysctl net.core.somaxconn net.ipv4.tcp_max_syn_backlog \
       net.ipv4.ip_local_port_range net.ipv4.tcp_tw_reuse

echo ""
echo "提示: 这些设置在重启后会失效"
echo "要永久保存，请运行: sudo sysctl -p"
```

---

## 五、你的RPC场景快速检查

### 5.1 检查服务端状态
```bash
# 1. 查看服务端监听状态
ss -ltn sport = :12345

# 2. 查看当前连接数
ss -tan dst :12345 | wc -l

# 3. 查看服务端accept队列是否满
ss -ltn sport = :12345 | awk 'NR==2 {print "Recv-Q:", $2, "Send-Q:", $3}'

# 4. 检查是否有队列溢出
netstat -s | grep -i "listen queue"
```

### 5.2 检查客户端状态
```bash
# 1. 查看TIME_WAIT数量
ss -tan state time-wait dport = :12345 | wc -l

# 2. 查看客户端本地端口使用
ss -tan dport = :12345 | awk '{print $4}' | cut -d: -f2 | sort | uniq | wc -l

# 3. 检查端口是否耗尽
# 可用端口数
upper=$(sysctl -n net.ipv4.ip_local_port_range | awk '{print $2}')
lower=$(sysctl -n net.ipv4.ip_local_port_range | awk '{print $1}')
total=$((upper - lower + 1))
echo "可用端口总数: $total"

# 已使用端口数
used=$(ss -tan | grep ESTAB | wc -l)
echo "已使用端口数: $used"
echo "使用率: $(echo "scale=2; $used * 100 / $total" | bc)%"
```

### 5.3 压测前后对比
```bash
# 压测前执行
echo "=== 压测前状态 ===" > /tmp/before.txt
netstat -s >> /tmp/before.txt

# 运行压测
./build/bin/test_rpc_bench -t 10 -c 1000

# 压测后执行
echo "=== 压测后状态 ===" > /tmp/after.txt
netstat -s >> /tmp/after.txt

# 对比差异
diff /tmp/before.txt /tmp/after.txt | grep -E "overflow|dropped|failed"
```

---

## 六、常见问题排查

### Q1: 参数修改后没生效？
```bash
# 检查是否有语法错误
sudo sysctl -p 2>&1 | grep -i error

# 检查是否被其他配置覆盖
grep -r "somaxconn" /etc/sysctl.d/
grep -r "somaxconn" /etc/sysctl.conf
```

### Q2: 队列还是溢出？
```bash
# 检查应用程序的listen backlog
# 在你的服务端代码中，acceptor初始化时可能指定了backlog
# 实际值 = min(程序指定值, somaxconn)

# 使用strace查看listen调用
sudo strace -p <server_pid> -e trace=listen 2>&1 | grep listen
```

### Q3: TIME_WAIT过多？
```bash
# 查看TIME_WAIT连接
ss -tan state time-wait | head -20

# 检查是否开启了timestamp（tw_reuse需要）
sysctl net.ipv4.tcp_timestamps

# 检查TIME_WAIT超时时间
sysctl net.ipv4.tcp_fin_timeout
```

---

## 七、实用别名（添加到 ~/.bashrc）

```bash
# 网络参数快捷查看
alias netparams='sysctl net.core.somaxconn net.ipv4.tcp_max_syn_backlog \
                 net.ipv4.ip_local_port_range net.ipv4.tcp_tw_reuse'

# 连接状态统计
alias conns='ss -s'

# TIME_WAIT数量
alias tw='ss -tan state time-wait | wc -l'

# 队列溢出检查
alias overflow='netstat -s | grep -E "overflow|dropped"'

# 服务端队列状态
alias queuestat='ss -ltn sport = :12345'

# 应用后
source ~/.bashrc
```

使用示例:
```bash
netparams    # 查看所有参数
conns        # 查看连接统计
tw           # 查看TIME_WAIT数量
overflow     # 查看溢出情况
```

---

需要我帮你运行这些命令检查当前状态吗？
