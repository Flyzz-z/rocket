#include "rocket/common/config.h"
#include "rocket/common/log.h"
#include "rocket/net/coder/abstract_protocol.h"
#include "rocket/net/coder/string_coder.h"
#include "rocket/net/coder/tinypb_coder.h"
#include "rocket/net/coder/tinypb_protocol.h"
#include "rocket/net/tcp/net_addr.h"
#include "rocket/net/tcp/tcp_client.h"
#include <arpa/inet.h>
#include <asio/awaitable.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>
#include <assert.h>
#include <fcntl.h>
#include <memory>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

void test_connect() {

  // 调用 conenct 连接 server
  // wirte 一个字符串
  // 等待 read 返回结果

  int fd = socket(AF_INET, SOCK_STREAM, 0);

  if (fd < 0) {
    ERRORLOG("invalid fd %d", fd);
    exit(0);
  }

  sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(12346);
  inet_aton("127.0.0.1", &server_addr.sin_addr);

  int rt = connect(fd, reinterpret_cast<sockaddr *>(&server_addr),
                   sizeof(server_addr));

  DEBUGLOG("connect success");

  std::string msg = "hello rocket!";

  rt = write(fd, msg.c_str(), msg.length());

  DEBUGLOG("success write %d bytes, [%s]", rt, msg.c_str());

  char buf[100];
  rt = read(fd, buf, 100);
  DEBUGLOG("success read %d bytes, [%s]", rt, std::string(buf).c_str());
}

// TODO 完善测试用例
asio::awaitable<void> test_tcp_client() {

  asio::ip::tcp::endpoint addr = asio::ip::tcp::endpoint(
      asio::ip::address_v4::from_string("127.0.0.1"), 8080);
  rocket::TcpClient client(addr);
  co_await client.connect();

  std::shared_ptr<rocket::TinyPBProtocol> message =
      std::make_shared<rocket::TinyPBProtocol>();
  message->m_msg_id = "123456789";
  message->m_pb_data = "test pb data";
  client.writeMessage(message, [](rocket::AbstractProtocol::s_ptr msg_ptr) {
    DEBUGLOG("send message success");
  });

  client.readMessage("123456789", [](rocket::AbstractProtocol::s_ptr msg_ptr) {
    std::shared_ptr<rocket::TinyPBProtocol> message =
        std::dynamic_pointer_cast<rocket::TinyPBProtocol>(msg_ptr);
    DEBUGLOG("msg_id[%s], get response %s", message->m_msg_id.c_str(),
             message->m_pb_data.c_str());
  });
	std::cout<<1<<std::endl;
	// sleep 10 s
	std::this_thread::sleep_for(std::chrono::seconds(10));
}

int main() {

  rocket::Config::SetGlobalConfig("../conf/rocket_client.xml");

  rocket::Logger::InitGlobalLogger(0);

  // test_connect();
	asio::io_context ioc;
  asio::co_spawn(ioc,[]()->auto{return test_tcp_client();},asio::detached);
	ioc.run();
  return 0;
}