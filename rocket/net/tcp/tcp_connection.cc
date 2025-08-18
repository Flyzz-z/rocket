#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/common/log.h"
#include "rocket/net/coder/string_coder.h"
#include "rocket/net/coder/tinypb_coder.h"
#include <asio/awaitable.hpp>
#include <asio/buffer.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/io_context.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <unistd.h>

namespace rocket {

TcpConnection::TcpConnection(asio::io_context *io_context, tcp::socket socket,
                             int buffer_size,
                             TcpConnectionType type /*= TcpConnectionByServer*/)
    : m_io_context(io_context), m_socket(std::move(socket)),
      m_timer(m_socket.get_executor()), m_in_buffer(buffer_size),
      m_out_buffer(buffer_size),m_connection_type(type) {

  m_local_addr = m_socket.local_endpoint();
  m_peer_addr = m_socket.remote_endpoint();
  m_timer.expires_at(std::chrono::steady_clock::time_point::max());
  m_coder = new TinyPBCoder();
  
}

TcpConnection::~TcpConnection() {
  DEBUGLOG("~TcpConnection");
  if (m_coder) {
    delete m_coder;
    m_coder = NULL;
  }
}

void TcpConnection::start() {
	asio::co_spawn(
			*m_io_context,
			[self = shared_from_this()]() -> awaitable<void> {
				return self->reader();
			},
			asio::detached);

	asio::co_spawn(
			*m_io_context,
			[self = shared_from_this()]() -> awaitable<void> {
				return self->writer();
			},
			asio::detached
	);
}

/**
 * 读协程，循环读取内容，每次读取完调用execute()
 */
awaitable<void> TcpConnection::reader() {
  // 不断循环读取，每次完成读取执行excute()
  try {
    for (;;) {
      if (!m_socket.is_open()) {
        ERRORLOG("onRead error, client has already disconneced, addr[%s]",
                 m_peer_addr.address().to_string().c_str());
        co_return;
      }
      // TODO 处理错误
      auto data_ptr = m_in_buffer.getBuffer().prepare(m_in_buffer.maxSize());
      auto bytes_read =
          co_await asio::async_read(m_socket, data_ptr,
                                    asio::transfer_at_least(1), use_awaitable);
      m_in_buffer.commit(bytes_read);
      std::cout<<bytes_read<<" "<<m_in_buffer.dataSize()<<std::endl;
      execute();
    }
  } catch (std::exception& e) {
    //TODO 停止？
    ERRORLOG("onRead error, client has already disconneced, addr[%s]",
             m_peer_addr.address().to_string().c_str());
  }
}

/**
 * 分 server 和 client 逻辑进行区分
 * 解码消息，进行不同处理
 */
void TcpConnection::execute() {
  if (m_connection_type == TcpConnectionByServer) {
    // 将 RPC 请求执行业务逻辑，获取 RPC 响应, 再把 RPC 响应发送回去
    std::vector<AbstractProtocol::s_ptr> result;
    m_coder->decode(result, m_in_buffer);
    for (size_t i = 0; i < result.size(); ++i) {
      // 1. 针对每一个请求，调用 rpc 方法，获取响应 message
      // 2. 将响应 message 放入到发送缓冲区，监听可写事件回包
      INFOLOG("success get request[%s] from client[%s]",
              result[i]->m_msg_id.c_str(), m_peer_addr.address().to_string().c_str());

      std::shared_ptr<TinyPBProtocol> message =
          std::make_shared<TinyPBProtocol>();
      // message->m_pb_data = "hello. this is rocket rpc test data";
      // message->m_msg_id = result[i]->m_msg_id;

      RpcDispatcher::GetRpcDispatcher()->dispatch(result[i], message, this);
    }

  } else {
    // 从 buffer 里 decode 得到 message 对象, 执行其回调
    std::vector<AbstractProtocol::s_ptr> result;
    m_coder->decode(result, m_in_buffer);

    for (size_t i = 0; i < result.size(); ++i) {
      std::string msg_id = result[i]->m_msg_id;
      auto it = m_read_dones.find(msg_id);
      if (it != m_read_dones.end()) {
        it->second(result[i]);
        m_read_dones.erase(it);
      }
    }
  }
}

/*
 * 服务端回复客户端
 */
void TcpConnection::reply(
    std::vector<AbstractProtocol::s_ptr> &replay_messages) {
  m_coder->encode(replay_messages, m_out_buffer);
  listenWrite();
}

/*
 * 写协程，分客户端和服务端端逻辑进行区分
 * 客户端：
 */
awaitable<void> TcpConnection::writer() {
  try {
    while (m_socket.is_open()) {

			if(m_out_buffer.dataSize() > 0 || m_write_dones.size() > 0) {
        // 客户端需要编码消息
        if (m_connection_type == TcpConnectionByClient) {
          //  1. 将 message encode 得到字节流
          // 2. 将字节流入到 buffer 里面，然后全部发送
          std::vector<AbstractProtocol::s_ptr> messages;

          for (size_t i = 0; i < m_write_dones.size(); ++i) {
            messages.push_back(m_write_dones[i].first);
          }

          m_coder->encode(messages, m_out_buffer);
        }

        // 错误处理
        std::size_t bytes_write = co_await asio::async_write(
            m_socket, m_out_buffer.getBuffer(), use_awaitable);
        m_out_buffer.consume(bytes_write);
				INFOLOG("write bytes: %ld, to endpoint[%s]", bytes_write,
                       m_peer_addr.address().to_string().c_str());
        if (m_connection_type == TcpConnectionByClient) {
          for (size_t i = 0; i < m_write_dones.size(); ++i) {
            m_write_dones[i].second(m_write_dones[i].first);
          }
          m_write_dones.clear();
        }
      } else {
        asio::error_code ec;
        co_await m_timer.async_wait(redirect_error(use_awaitable, ec));
      }
      
    }
  } catch (std::exception& e) { 
    //TODO 错误处理
    ERRORLOG("TcpConnection::writer error, error info: %s", e.what());
  }
}

void TcpConnection::setState(const TcpState state) { m_state = Connected; }

TcpState TcpConnection::getState() { return m_state; }

void TcpConnection::clear() {
  // 处理一些关闭连接后的清理动作
  if (m_state == Closed) {
    return;
  }
  m_state = Closed;
}

void TcpConnection::shutdown() {
  if (m_state == Closed || m_state == NotConnected) {
    return;
  }

  // 处于半关闭
  m_state = HalfClosing;

  // 调用 shutdown 关闭读写，意味着服务器不会再对这个 fd 进行读写操作了
  // 发送 FIN 报文， 触发了四次挥手的第一个阶段
  // 当 fd 发生可读事件，但是可读的数据为0，即 对端发送了 FIN
  // ::shutdown(m_fd, SHUT_RDWR);
}

void TcpConnection::setConnectionType(TcpConnectionType type) {
  m_connection_type = type;
}

void TcpConnection::listenWrite() {
  m_timer.cancel_one();
}

void TcpConnection::listenRead() {}

void TcpConnection::pushSendMessage(
    AbstractProtocol::s_ptr message,
    std::function<void(AbstractProtocol::s_ptr)> done) {
  m_write_dones.push_back(std::make_pair(message, done));
}

void TcpConnection::pushReadMessage(
    const std::string &msg_id,
    std::function<void(AbstractProtocol::s_ptr)> done) {
  m_read_dones.insert(std::make_pair(msg_id, done));
}

tcp::endpoint TcpConnection::getLocalAddr() { return m_local_addr; }

tcp::endpoint TcpConnection::getPeerAddr() { return m_peer_addr; }

} // namespace rocket