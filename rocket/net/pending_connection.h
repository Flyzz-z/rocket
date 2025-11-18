#ifndef ROCKET_NET_PENDING_CONNECTION_H
#define ROCKET_NET_PENDING_CONNECTION_H

#include <memory>

namespace rocket {

// 前向声明
class TcpConnection;

/**
 * PendingConnection 结构体
 * 用于在 accept 线程和 IO 线程之间传递待启动的 TcpConnection
 * accept 线程负责：accept() + 创建 TcpConnection 对象
 * IO 线程负责：调用 connection->start() 启动读写协程
 */
struct PendingConnection {
  std::shared_ptr<TcpConnection> connection;

  PendingConnection() = default;

  explicit PendingConnection(std::shared_ptr<TcpConnection> conn)
      : connection(std::move(conn)) {}

	PendingConnection(const PendingConnection& rhs) = delete;
	PendingConnection& operator=(const PendingConnection& rhs) = delete;

	PendingConnection(PendingConnection&& rhs) noexcept
		: connection(std::move(rhs.connection)) {}
	PendingConnection& operator=(PendingConnection&& rhs) {
		connection = std::move(rhs.connection);
		return *this;
	}
};

} // namespace rocket

#endif
