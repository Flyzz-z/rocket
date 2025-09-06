#include "rocket/net/rpc/etcd_registry.h"
#include "rocket/common/config.h"
#include "rocket/common/log.h"
#include <etcd/Response.hpp>
#include <etcd/KeepAlive.hpp>
#include <memory>
#include <string>

namespace rocket {
EtcdRegistry::EtcdRegistry() {}

EtcdRegistry::~EtcdRegistry() {}

void EtcdRegistry::init(const std::string &ip, int port,
                        const std::string &username,
                        const std::string &password) {
	auto instance = EtcdRegistry::GetInstance();
  std::string addr = ip + ":" + std::to_string(port);
	instance->m_etcd_client =  std::make_unique<etcd::Client>(addr, username, password);
  instance->m_ip = ip;
  instance->m_port = port;
}

void EtcdRegistry::initFromConfig() {
  auto config = Config::GetGlobalConfig();
  init(config->m_etcd_config.ip, config->m_etcd_config.port,
       config->m_etcd_config.username, config->m_etcd_config.password);
	registerServicesFromConfig();
}

bool EtcdRegistry::registerService(const std::string &service_name,
                                   const std::string &service_ip,
                                   int service_port) {
					
  try {
    string addr = service_ip + ":" + std::to_string(service_port);
    string key = "/rocket/service/" + service_name + "/" + addr;
    string val = addr;

		// 创建租约,自动续约
		DEBUGLOG("create lease");
		std::shared_ptr<etcd::KeepAlive> keep_alive =
      m_etcd_client->leasekeepalive(30).get();
		m_keep_alives[key] = keep_alive;

    etcd::Response res = m_etcd_client->set(key, val,keep_alive->Lease()).get();
    if (res.is_ok()) {
			DEBUGLOG("register service %s success")
      return true;
    } else {
      ERRORLOG("register service %s failed, why %s", service_name.c_str(),
               res.error_message().c_str());
    }
  } catch (const std::exception &e) {
    ERRORLOG("register service %s failed, why", service_name.c_str(), e.what());
  }
  return false;
}

// 该函数暂未使用
// TODO 待添加处理租约
void EtcdRegistry::unregisterService(const string &service_name) {
  try {
    string addr = m_ip + ":" + std::to_string(m_port);
    string key = "/rocket/service/" + service_name + "/" + addr;
    etcd::Response res = m_etcd_client->rm(key).get();

  } catch (const std::exception &e) {
    ERRORLOG("unregister service %s failed, why", service_name.c_str(),
             e.what());
  }
  // TODO 没有处理失败情况
}

string EtcdRegistry::discoverService(const string &service_name) {
  try {
    std::string key = "/rocket/service/" + service_name;
    etcd::Response res = m_etcd_client->ls(key).get();
    if (res.is_ok()) {
      if (res.keys().size() > 0) {
        DEBUGLOG("etcd get service %s success", service_name.c_str());
        return res.values()[0].as_string();
      } else {
        ERRORLOG("etcd service %s not found", service_name.c_str());
      }
    } else {
      ERRORLOG("etcd discover service %s failed, why %s", service_name.c_str(),
               res.error_message().c_str());
    }
  } catch (const std::exception &e) {
    ERRORLOG("etcd discover service %s failed, why", service_name.c_str(),
             e.what());
  }
  return "";
}

void EtcdRegistry::registerServicesFromConfig() {

  auto config = Config::GetGlobalConfig();
	auto instance = EtcdRegistry::GetInstance();
  for (const auto &[service_name, stub] : config->m_rpc_stubs) {
    instance->registerService(service_name, stub.addr.address().to_string(),
                    stub.addr.port());
  }
}

} // namespace rocket