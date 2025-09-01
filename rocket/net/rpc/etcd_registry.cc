#include "rocket/net/rpc/etcd_registry.h"
#include "rocket/common/config.h"
#include "rocket/common/log.h"
#include <etcd/Response.hpp>
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

bool EtcdRegistry::registerService(const std::string &service_name,
                                   const std::string &service_ip,
                                   int service_port) {

  try {
    string addr = service_ip + ":" + std::to_string(service_port);
    string key = "/rocket/service/" + service_name + "/" + addr;
    string vale = addr;
    etcd::Response res = m_etcd_client->set(key, vale).get();
    if (res.is_ok()) {
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