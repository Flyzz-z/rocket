#include "rocket/net/rpc/etcd_registry.h"
#include "rocket/common/config.h"
#include "rocket/common/log.h"
#include <atomic>
#include <etcd/Response.hpp>
#include <etcd/KeepAlive.hpp>
#include <memory>
#include <mutex>
#include <string>

namespace rocket {
EtcdRegistry::EtcdRegistry() : m_services_ac(std::make_shared<service_map>()),m_services_bk(std::make_shared<service_map>()) {}

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

/*
	优先从本地缓存获取，不存在则从etcd获取
*/
std::vector<string> EtcdRegistry::discoverService(const string &service_name) {


	// 优先缓存获取
	auto services_map = std::atomic_load(&m_services_ac);
	if(services_map->count(service_name)) {
		return services_map->at(service_name);
	}
	std::vector<std::string> vec = loadByKey(service_name);
	// 更新缓存
	if(vec.size() > 0) {
		std::atomic_exchange(&m_services_ac, m_services_bk);
		m_services_bk = std::make_shared<service_map>(*(std::atomic_load(&m_services_ac)));
	}

	DEBUGLOG("discoverService service_name: %s, vec.size(): %d",service_name.c_str(), vec.size());
  return vec;
}

std::vector<string> EtcdRegistry::loadByKey(const string &service_name) {
  try {
    std::string key = "/rocket/service/" + service_name;
    etcd::Response res = m_etcd_client->ls(key).get();
    if (res.is_ok()) {
      if (res.keys().size() > 0) {
        DEBUGLOG("etcd get service %s success", service_name.c_str());
				std::vector<std::string> vec;
				for(auto val : res.values()) {
					vec.emplace_back(val.as_string());
				}

				// 设置bk缓存
				std::unique_lock<std::mutex> lk(m_bk_mutex);
				(*m_services_bk)[service_name] = vec;
        return vec;
				
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
	return {};
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