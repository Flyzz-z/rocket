#ifndef ROCKET_NET_TCP_TCP_BUFFER_H
#define ROCKET_NET_TCP_TCP_BUFFER_H 


#include <asio/buffer.hpp>
#include <cstddef>
#include <vector>
namespace rocket {

using TcpDataBuffer = asio::dynamic_vector_buffer<char, std::allocator<char>>;

class TcpBuffer {

public:
	TcpBuffer(int size);

	std::size_t dataSize();

	std::size_t maxSize();

	void writeToBuffer(const char* buf, std::size_t size);

	void readFromBuffer(std::vector<char> &re, std::size_t size);

	std::vector<char> getBufferVecCopy();

	TcpDataBuffer& getBuffer();

	void consume(std::size_t size);

	void commit(std::size_t size);
private: 
	std::vector<char> m_buffer_vector;
	TcpDataBuffer m_buffer;
	std::size_t m_size;
};



}

#endif