#ifndef ROCKET_NET_RPC_ETCD_REGISTRY_H
#define ROCKET_NET_RPC_ETCD_REGISTRY_H

#include "etcd/Client.hpp"
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

  std::vector<std::string> discoverService(const std::string &service_name);

	std::vector<std::string> loadByKey(const std::string &service_name);
	//TODO 添加定期从etcd更新服务信息

private:
  std::string m_name;
  std::string m_ip;
  int m_port;
  std::string m_username;
  std::string m_password;
  std::unique_ptr<etcd::Client> m_etcd_client;
  std::unordered_map<std::string, std::shared_ptr<etcd::KeepAlive>>
      m_keep_alives;

	/*
		ac使用atomic操作
		bk使用lock
	 */
  using service_map = std::unordered_map<std::string, std::vector<std::string>>;
 	std::shared_ptr<service_map> m_services_ac;
  std::shared_ptr<service_map> m_services_bk;
	// 写bk的锁
	std::mutex m_bk_mutex;
};

} // namespace rocket

#endif