#include "net/tcp/tcp_buffer.h"
#include <asio/buffer.hpp>
#include <cstring>
#include <iostream>

namespace rocket {

TcpBuffer::TcpBuffer(int size)
    : buffer_(asio::dynamic_buffer(buffer_vector_)), size_(size) {
    }
std::size_t TcpBuffer::dataSize() { return buffer_.size(); }

std::size_t TcpBuffer::maxSize() { return size_; }

void TcpBuffer::readFromBuffer(std::vector<char> &re, std::size_t size) {
  if (size <= 0)
    return;

  auto read_size = std::min(size, buffer_.size());
  const char *data_ptr = asio::buffer_cast<const char *>(buffer_.data());
  std::memcpy(re.data(), data_ptr, read_size);
  buffer_.consume(read_size);
}

void TcpBuffer::writeToBuffer(const char *buf, std::size_t size) {
  if (size <= 0)
    return;
  auto mutable_buf = buffer_.prepare(size);
  std::size_t bytes_copied =
      asio::buffer_copy(mutable_buf,            // 目标：可写区域
                        asio::buffer(buf, size) // 源：输入数据
      );
  buffer_.commit(bytes_copied);
}

TcpDataBuffer &TcpBuffer::getBuffer() { return buffer_; }

std::vector<char> TcpBuffer::getBufferVecCopy() {
  std::vector<char> re(buffer_.data().size());
  const char *data_ptr = asio::buffer_cast<const char *>(buffer_.data());
  re.assign(data_ptr, data_ptr + buffer_.data().size());
  return re;
}

void TcpBuffer::consume(std::size_t size) { buffer_.consume(size); }

void TcpBuffer::commit(std::size_t size) { buffer_.commit(size); }

} // namespace rocket
