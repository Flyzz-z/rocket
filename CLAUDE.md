# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Rocket is a C++20 coroutine-based RPC framework built on top of ASIO's network library. It implements a Reactor pattern with coroutine support for both server and client, featuring service registration/discovery via etcd, custom protocol encoding (TinyPB), and asynchronous logging.

Key features:
- C++20 coroutines with ASIO for async I/O
- Main-Sub Reactor network model (multi-threaded event loops)
- etcd-based service registry with health checking, caching, and load balancing
- Protobuf-based RPC communication with custom TinyPB protocol
- Asynchronous logging system with modern C++ concurrency APIs

## Build Commands

```bash
# Full clean build (recommended)
./build.sh              # Debug build
./build.sh Release      # Release build

# Manual CMake build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make -j$(nproc)

# Install library (to /usr/local)
cd build && sudo make install
```

Executables are output to `build/bin/`:
- `test_log` - Test logging system
- `test_tcp` - Test TCP server/client
- `test_client` - Test basic client
- `test_rpc_server` - RPC server with etcd registry
- `test_rpc_client` - RPC client with service discovery

## Running Tests

```bash
# Run RPC server (requires etcd running on localhost:2379)
./build/bin/test_rpc_server ./conf/rocket.xml

# Run RPC client (in another terminal)
./build/bin/test_rpc_client

# Run TCP server test
./build/bin/test_tcp

# Run basic client test
./build/bin/test_client

# Run logging test
./build/bin/test_log
```

## Configuration

Configuration is XML-based (see `conf/rocket.xml`):
- **Log settings**: log level, file path, max file size, sync interval
- **Server settings**: port, number of I/O threads
- **etcd settings**: IP, port, credentials for service registry
- **Service stubs**: downstream RPC service endpoints

## Architecture

### Coroutine Model

The project uses a custom C++20 coroutine wrapper built around ASIO:

- **task<T>**: Coroutine return type with `promise_type` and awaiter interface
- **promise_type**: Manages coroutine lifecycle with `continuation_` for symmetric transfer
- **AsyncSyscall**: Base template for async operations (Accept, Send, Recv) that suspend on EAGAIN/EWOULDBLOCK
- Coroutines chain via `await_suspend()` returning continuation handles

See README.md for detailed coroutine implementation explanation.

### Network Model - Reactor Pattern

- **EventLoop** (`rocket/net/event_loop.h`): Wraps `asio::io_context`, manages coroutine spawning and timers
  - Each thread has its own EventLoop via thread-local storage
  - Supports work guards for long-running scenarios (IOThread)

- **IOThread** (`rocket/net/io_thread.h`): Worker thread running an EventLoop

- **IOThreadGroup** (`rocket/net/io_thread_group.h`): Pool of IOThreads for sub-reactors
  - Round-robin distribution via `getIOThread()`

- **TcpServer** (`rocket/net/tcp/tcp_server.h`):
  - Main Reactor: Runs acceptor in main event loop
  - Sub Reactors: Distributes connections to IOThreadGroup
  - Manages active connections in `clients_` set

- **TcpConnection** (`rocket/net/tcp/tcp_connection.h`): Represents a single connection

- **TcpClient** (`rocket/net/tcp/tcp_client.h`): Client-side connection management

### RPC Framework

- **Protocol Layer** (`rocket/net/coder/`):
  - `TinyPBProtocol`: Custom protocol with start/end markers, message ID, method name, error codes, protobuf data, checksum
  - `TinyPBCoder`: Encodes/decodes TinyPBProtocol messages
  - `AbstractProtocol`/`AbstractCoder`: Base interfaces

- **RPC Layer** (`rocket/net/rpc/`):
  - `RpcChannel`: Client-side channel implementing `google::protobuf::RpcChannel`
    - `FindAddr()`: Resolves service name to endpoints (via etcd or config)
    - `CallMethod()`: Sends RPC request and handles response
  - `RpcDispatcher`: Server-side dispatcher routes requests to registered service implementations
  - `RpcController`: Implements `google::protobuf::RpcController` for timeout, error handling, msg_id
  - `RpcClosure`: Callback wrapper

- **Service Registry** (`rocket/net/rpc/etcd_registry.h`):
  - Service registration with lease-based heartbeat via `etcd::KeepAlive`
  - Service discovery with local caching
  - Watcher for service updates (invalidates cache on changes)
  - Round-robin load balancing via `nameToIndex()`
  - Singleton pattern for global access

### Logging

- **Logger** (`rocket/common/log.h`, `log.cc`):
  - Async logging with producer-consumer pattern
  - Separate thread for log writing (optional via `InitGlobalLogger(1)`)
  - Sync/async modes for app logs and framework logs
  - Macros: `DEBUGLOG`, `INFOLOG`, `ERRORLOG`, `APPDEBUGLOG`
  - Thread-safe queue with semaphore synchronization

### Common Utilities

- `Config` (`rocket/common/config.h`): XML configuration parser using tinyxml
- `Singleton` (`rocket/common/singleton.h`): CRTP singleton base class
- `Mutex` (`rocket/common/mutex.h`): Scoped mutex wrappers
- `RunTime` (`rocket/common/run_time.h`): Runtime context storage
- `msg_id_util` (`rocket/common/msg_id_util.h`): Message ID generation

## Protobuf Code Generation

To add new RPC services:

1. Define `.proto` file in `testcases/proto/`
2. Generate code: `protoc --cpp_out=. order.proto`
3. Generate coroutine stub wrapper in `testcases/proto/co_stub/` (manually or via custom generator)
4. Add to CMakeLists.txt executable dependencies

## Dependencies

External libraries (installed in `/usr/local/lib`):
- **ASIO** (standalone, header-only expected)
- **Protobuf** (`libprotobuf.a`)
- **TinyXML** (`libtinyxml.a`)
- **etcd-cpp-api** (`libetcd-cpp-api.so`)

Requires:
- C++20 compiler (uses Clang++ by default)
- etcd server running for service registry tests

## Key Design Patterns

1. **Symmetric Transfer**: Coroutines use `await_suspend()` returning `coroutine_handle<>` to avoid stack buildup
2. **CRTP**: Used in AsyncSyscall template and Singleton
3. **Main-Sub Reactor**: Accept in main loop, distribute connections to worker threads
4. **Service Locator**: etcd registry with local caching and watch-based invalidation
5. **Producer-Consumer**: Async logging with blocking queue

## Common Gotchas

- EventLoop must be initialized via `EventLoop::getThreadEventLoop()` for thread-local storage
- RPC services must be registered with `RpcDispatcher::registerService()` before server starts
- etcd must be running and accessible for RPC discovery to work (or use direct IP:port in config)
- Async operations (Accept, Send, Recv) expect non-blocking sockets managed by ASIO
- Log files written to `log/` directory (must exist)
- TinyPB protocol has start byte (0x02) and end byte (0x03) markers
