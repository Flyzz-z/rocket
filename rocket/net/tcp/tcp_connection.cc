#include "rocket/net/tcp/tcp_connection.h"
#include "rocket/common/log.h"
#include "rocket/net/coder/string_coder.h"
#include "rocket/net/coder/tinypb_coder.h"
#include "rocket/net/fd_event_group.h"
#include <asio/buffer.hpp>
#include <asio/io_context.hpp>
#include <asio/read.hpp>
#include <memory>
#include <unistd.h>

namespace rocket {

TcpConnection::TcpConnection(asio::io_context *io_context, tcp::socket socket,
                             int buffer_size,
                             TcpConnectionType type /*= TcpConnectionByServer*/)
    : m_io_context(io_context), m_socket(std::move(socket)),
      m_in_buffer(asio::dynamic_buffer(m_in_vector, buffer_size)),
      m_out_buffer(asio::dynamic_buffer(m_out_vector, buffer_size)),
      m_connection_type(type) {

  m_local_addr = socket.local_endpoint();
  m_peer_addr = socket.remote_endpoint();

  m_coder = new TinyPBCoder();

  if (m_connection_type == TcpConnectionByServer) {
    // TODO 启动读协程
  }
}

TcpConnection::~TcpConnection() {
  DEBUGLOG("~TcpConnection");
  if (m_coder) {
    delete m_coder;
    m_coder = NULL;
  }
}

/**
 * 读协程，循环读取内容，每次读取完调用execute()
 */
awaitable<void> TcpConnection::reader() {
  // 不断循环读取，每次完成读取执行excute()
  for (;;) {
    if (m_state != Connected) {
      ERRORLOG("onRead error, client has already disconneced, addr[%s]",
               m_peer_addr.address().to_string());
      co_return;
    }
    // TODO 处理错误
    co_await asio::async_read(m_socket, m_in_buffer, asio::transfer_at_least(1),
                              use_awaitable);

    execute();
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
              result[i]->m_msg_id.c_str(), m_peer_addr.address().to_string());

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

void TcpConnection::reply(
    std::vector<AbstractProtocol::s_ptr> &replay_messages) {
  m_coder->encode(replay_messages, m_out_buffer);
  listenWrite();
}

/*
 * 写协程，分客户端和服务端端逻辑进行区分
 TODO: 完成写协程
 * 客户端：
 */
awaitable<void> TcpConnection::writer() {
  for (;;) {
    if (m_state != Connected) {
      ERRORLOG("onWrite error, client has already disconneced, addr[%s]",
               m_peer_addr.address().to_string());
      co_return;
    }

		if(m_connection_type == TcpConnectionByClient) {
			

		} else {


		}
  }
}

void TcpConnection::onWrite() {
  // 将当前 out_buffer 里面的数据全部发送给 client

  if (m_state != Connected) {
    ERRORLOG(
        "onWrite error, client has already disconneced, addr[%s], clientfd[%d]",
        m_peer_addr.address().to_string(), m_fd);
    return;
  }

  if (m_connection_type == TcpConnectionByClient) {
    //  1. 将 message encode 得到字节流
    // 2. 将字节流入到 buffer 里面，然后全部发送

    std::vector<AbstractProtocol::s_ptr> messages;

    for (size_t i = 0; i < m_write_dones.size(); ++i) {
      messages.push_back(m_write_dones[i].first);
    }

    m_coder->encode(messages, m_out_buffer);
  }

  bool is_write_all = false;
  while (true) {
    if (m_out_buffer->readAble() == 0) {
      DEBUGLOG("no data need to send to client [%s]",
               m_peer_addr.address().to_string());
      is_write_all = true;
      break;
    }
    int write_size = m_out_buffer->readAble();
    int read_index = m_out_buffer->readIndex();

    int rt = write(m_fd, &(m_out_buffer->m_buffer[read_index]), write_size);

    if (rt >= write_size) {
      DEBUGLOG("no data need to send to client [%s]",
               m_peer_addr.address().to_string());
      is_write_all = true;
      break;
    }
    if (rt == -1 && errno == EAGAIN) {
      // 发送缓冲区已满，不能再发送了。
      // 这种情况我们等下次 fd 可写的时候再次发送数据即可
      ERRORLOG("write data error, errno==EAGIN and rt == -1");
      break;
    }
  }
  if (is_write_all) {
    m_fd_event->cancle(FdEvent::OUT_EVENT);
    // m_event_loop->addEvent(m_fd_event);
  }

  if (m_connection_type == TcpConnectionByClient) {
    for (size_t i = 0; i < m_write_dones.size(); ++i) {
      m_write_dones[i].second(m_write_dones[i].first);
    }
    m_write_dones.clear();
  }
}

void TcpConnection::setState(const TcpState state) { m_state = Connected; }

TcpState TcpConnection::getState() { return m_state; }

void TcpConnection::clear() {
  // 处理一些关闭连接后的清理动作
  if (m_state == Closed) {
    return;
  }
  m_fd_event->cancle(FdEvent::IN_EVENT);
  m_fd_event->cancle(FdEvent::OUT_EVENT);

  // m_event_loop->deleteEvent(m_fd_event);

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
  ::shutdown(m_fd, SHUT_RDWR);
}

void TcpConnection::setConnectionType(TcpConnectionType type) {
  m_connection_type = type;
}

void TcpConnection::listenWrite() {

  m_fd_event->listen(FdEvent::OUT_EVENT,
                     std::bind(&TcpConnection::onWrite, this));
  // m_event_loop->addEvent(m_fd_event);
}

void TcpConnection::listenRead() {

  m_fd_event->listen(FdEvent::IN_EVENT,
                     std::bind(&TcpConnection::onRead, this));
  // m_event_loop->addEvent(m_fd_event);
}

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

int TcpConnection::getFd() { return m_fd; }

} // namespace rocket