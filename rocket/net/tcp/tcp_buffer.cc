#include "net/tcp/tcp_buffer.h"
#include <asio/buffer.hpp>
#include <cstring>

namespace rocket {

TcpBuffer::TcpBuffer(int size)
    : m_buffer(asio::dynamic_buffer(m_buffer_vector, size)), m_size(size) {}
std::size_t TcpBuffer::dataSize() { return m_buffer.size(); }

void TcpBuffer::readFromBuffer(std::vector<char> &re, std::size_t size) {
  if (size <= 0)
    return;

  auto read_size = std::min(size, m_buffer.size());
  const char *data_ptr = asio::buffer_cast<const char *>(m_buffer.data());
  std::memcpy(re.data(), data_ptr, read_size);
  m_buffer.consume(read_size);
}

void TcpBuffer::writeToBuffer(const char *buf, std::size_t size) {
  if (size <= 0)
    return;
  auto mutable_buf = m_buffer.prepare(size);
  std::size_t bytes_copied =
      asio::buffer_copy(mutable_buf,            // 目标：可写区域
                        asio::buffer(buf, size) // 源：输入数据
      );
  m_buffer.commit(bytes_copied);
}

TcpDataBuffer &TcpBuffer::getBuffer() { return m_buffer; }

std::vector<char> TcpBuffer::getBufferVecCopy() {
  std::vector<char> re(m_buffer.size());
  const char *data_ptr = asio::buffer_cast<const char *>(m_buffer.data());
  re.assign(data_ptr, data_ptr + m_buffer.size());
  return re;
}

void TcpBuffer::consume(std::size_t size) { m_buffer.consume(size); }

void TcpBuffer::commit(std::size_t size) { m_buffer.commit(size); }

} // namespace rocket
