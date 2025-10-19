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
  std::string name_;
  std::string ip_;
  int port_;
  std::string username_;
  std::string password_;
  std::unique_ptr<etcd::Client> etcd_client_;
  std::unordered_map<std::string, std::shared_ptr<etcd::KeepAlive>>
      keep_alives_;

	/*
		ac使用atomic操作
		bk使用lock
	 */
  using service_map = std::unordered_map<std::string, std::vector<std::string>>;
 	std::shared_ptr<service_map> services_ac_;
  std::shared_ptr<service_map> services_bk_;
	// 写bk的锁
	std::mutex bk_mutex_;

	
};

} // namespace rocket

#endif