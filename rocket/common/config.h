#ifndef ROCKET_COMMON_CONFIG_H
#define ROCKET_COMMON_CONFIG_H

#include <asio/ip/tcp.hpp>
#include <map>
#include <tinyxml/tinyxml.h>

namespace rocket {

using asio::ip::tcp;

struct RpcStub {
  std::string name;
  tcp::endpoint addr;
  int timeout{2000};
};

struct EtcdConfig {
  std::string ip;
  int port{0};
  std::string username;
  std::string password;
};

class Config {
public:
  Config(const char *xmlfile);

  Config();

  ~Config();

public:
  static Config *GetGlobalConfig();
  static void SetGlobalConfig(const char *xmlfile);

public:
  std::string log_level_;
  std::string log_file_name_;
  std::string log_file_path_;
  int log_max_file_size_{0};
  int log_sync_inteval_{0}; // 日志同步间隔，ms

  int port_{0};
  int io_threads_{0};

  TiXmlDocument *xml_document_{NULL};

  std::map<std::string, RpcStub> rpc_stubs_;

	EtcdConfig etcd_config_;
};

} // namespace rocket

#endif