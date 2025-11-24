#include "rocket/log/log.h"
#include "rocket/net/tcp/tcp_server.h"
#include "rocket/common/config.h"

void test_tcp_server() {

  auto addr = asio::ip::tcp::endpoint(asio::ip::address_v4::any(), 8080);

  DEBUGLOG("create addr %s", addr.address().to_string().c_str());

  rocket::TcpServer tcp_server(addr);

  tcp_server.start();

}

int main() {

  rocket::Config::SetGlobalConfig("../conf/rocket.xml");
  // rocket::Logger::InitGlobalLogger();

  //rocket::Config::SetGlobalConfig(NULL);

  rocket::Logger::InitGlobalLogger(0);

  test_tcp_server();
  
}