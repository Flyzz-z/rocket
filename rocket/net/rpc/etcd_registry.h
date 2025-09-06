#ifndef ROCKET_NET_RPC_ETCD_REGISTRY_H
#define ROCKET_NET_RPC_ETCD_REGISTRY_H

#include "etcd/Client.hpp"
#include "io_thread.h"
#include "singleton.h"
#include <etcd/KeepAlive.hpp>
#include <memory>
#include <string>
#include <unordered_map>

namespace rocket {

class EtcdRegistry : public Singleton<EtcdRegistry> {
public:
  EtcdRegistry();
  ~EtcdRegistry();

  static void init(const std::string &ip, int port, const std::string &username,
                   const std::string &password);
	static void initFromConfig();
  static void registerServicesFromConfig();

  bool registerService(const std::string &service_name,
                       const std::string &service_ip, int service_port);

  void unregisterService(const std::string &service_name);

  string discoverService(const std::string &service_name);

private:
  std::string m_name;
  std::string m_ip;
  int m_port;
  std::string m_username;
  std::string m_password;
  std::unique_ptr<etcd::Client> m_etcd_client;
	std::unordered_map<std::string, std::shared_ptr<etcd::KeepAlive>> m_keep_alives;
};

} // namespace rocket

#endif