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

// 服务端提供的服务配置
struct ServiceConfig {
  std::string name;  // 服务名称
  std::string ip;    // 服务IP
  int port{0};       // 服务端口
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

  // 客户端调用的下游服务配置(用于服务发现)
  std::map<std::string, RpcStub> rpc_stubs_;

  // 服务端提供的服务列表(需要注册到etcd)
  std::vector<ServiceConfig> provided_services_;

	EtcdConfig etcd_config_;
};

} // namespace rocket

#endif