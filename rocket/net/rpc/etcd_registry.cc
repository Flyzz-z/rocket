#include "rocket/net/rpc/etcd_registry.h"
#include "rocket/common/config.h"
#include "rocket/common/log.h"
#include <atomic>
#include <etcd/KeepAlive.hpp>
#include <etcd/Response.hpp>
#include <etcd/Watcher.hpp>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace rocket {
EtcdRegistry::EtcdRegistry()
    : all_service_map_(bucket_size_), bucket_mutex_(bucket_size_),
      watching_(false) {}
EtcdRegistry::~EtcdRegistry() { stopWatcher(); }

void EtcdRegistry::init(const std::string &ip, int port,
                        const std::string &username,
                        const std::string &password) {
  auto instance = EtcdRegistry::GetInstance();
  std::string addr = ip + ":" + std::to_string(port);
  instance->etcd_client_ =
      std::make_unique<etcd::Client>(addr, username, password);
  instance->ip_ = ip;
  instance->port_ = port;

	instance->startWatcher();
}

void EtcdRegistry::initFromConfig() {
  auto config = Config::GetGlobalConfig();
  init(config->etcd_config_.ip, config->etcd_config_.port,
       config->etcd_config_.username, config->etcd_config_.password);
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
        etcd_client_->leasekeepalive(30).get();
    keep_alives_[key] = keep_alive;

    etcd::Response res = etcd_client_->set(key, val, keep_alive->Lease()).get();
    if (res.is_ok()) {
      DEBUGLOG("register service %s success", service_name.c_str());
      return true;
    } else {
      ERRORLOG("register service %s failed, why %s", service_name.c_str(),
               res.error_message().c_str());
    }
  } catch (const std::exception &e) {
    ERRORLOG("register service %s failed, why: %s", service_name.c_str(),
             e.what());
  }
  return false;
}

// 该函数暂未使用
void EtcdRegistry::unregisterService(const string &service_name) {
  try {
    string addr = ip_ + ":" + std::to_string(port_);
    string key = "/rocket/service/" + service_name + "/" + addr;
    etcd::Response res = etcd_client_->rm(key).get();

  } catch (const std::exception &e) {
    ERRORLOG("unregister service %s failed, why: %s", service_name.c_str(),
             e.what());
  }
}

/*
        优先从本地缓存获取，不存在则从etcd获取
*/
std::vector<string> EtcdRegistry::discoverService(const string &service_name) {

  auto id = nameToIndex(service_name);
  std::scoped_lock<std::mutex> lock(bucket_mutex_[id]);
  auto &keyServiceMap = all_service_map_[id];
  if (keyServiceMap.count(service_name)) {
    return keyServiceMap[service_name];
  }

  // 没有缓存则加载
  std::vector<std::string> vec = loadByKey(service_name);
  keyServiceMap[service_name] = vec;
  return vec;
}

std::vector<string> EtcdRegistry::loadByKey(const string &service_name) {
  try {
    std::string key = "/rocket/service/" + service_name;
    etcd::Response res = etcd_client_->ls(key).get();
    if (res.is_ok()) {
      if (res.keys().size() > 0) {
        DEBUGLOG("etcd get service %s success", service_name.c_str());
        std::vector<std::string> vec;
        for (auto val : res.values()) {
          vec.emplace_back(val.as_string());
        }
        return vec;
      } else {
        ERRORLOG("etcd service %s not found", service_name.c_str());
      }
    } else {
      ERRORLOG("etcd discover service %s failed, why %s", service_name.c_str(),
               res.error_message().c_str());
    }
  } catch (const std::exception &e) {
    ERRORLOG("etcd discover service %s failed, why: %s", service_name.c_str(),
             e.what());
  }
  return {};
}

void EtcdRegistry::startWatcher() {
  if (watching_.load()) {
    INFOLOG("Watcher is already running");
    return;
  }

  watching_.store(true);
	DEBUGLOG("begin start watcher");
  // 在单独的线程中启动watcher，避免阻塞
  watcher_thread_ = std::make_unique<std::thread>([this]() {
    try {
      // 监听/rocket/service/前缀下的所有变化
      std::string watch_prefix = "/rocket/service/";

      // 创建处理函数
      std::function<void(etcd::Response)> callback =
          [this](etcd::Response response) { handleWatchEvent(response); };

      // 启动watcher
      watcher_ =
          std::make_unique<etcd::Watcher>(*etcd_client_, watch_prefix, callback,
                                          true); // true表示监听前缀下的所有变化

      INFOLOG("Etcd watcher started for prefix: %s", watch_prefix.c_str());      
      // 保持watcher运行
      watcher_->Wait();

    } catch (const std::exception &e) {
      ERRORLOG("Etcd watcher error: %s", e.what());
      watching_.store(false);
    }
  });

  INFOLOG("Etcd watcher thread started");
}

void EtcdRegistry::stopWatcher() {
  if (watching_.load()) {
    watching_.store(false);

    if (watcher_) {
      // 注意：etcd-cpp-api的Watcher可能没有直接的停止方法
      // 这里可能需要根据具体实现进行调整
      watcher_.reset();
    }

    if (watcher_thread_ && watcher_thread_->joinable()) {
      watcher_thread_->join();
      watcher_thread_.reset();
    }

    INFOLOG("Etcd watcher stopped");
  }
}

void EtcdRegistry::handleWatchEvent(etcd::Response response) {
  try {
    INFOLOG("Received etcd watch event, action: %s",
            response.action().c_str());
    // 检查事件类型
    if (response.action() == "delete" || response.action() == "expire") {
      // 服务被删除或过期
      std::string key = response.key(0);
      INFOLOG("Service removed, key: %s", key.c_str());
      removeServiceFromCache(key);
    } else if (response.action() == "set" || response.action() == "update") {
      // 服务被创建或更新
      INFOLOG("Service updated");
      // 可以选择刷新相关服务的缓存
    }
  } catch (const std::exception &e) {
    ERRORLOG("Error handling watch event: %s", e.what());
  }
}

void EtcdRegistry::removeServiceFromCache(const std::string &key) {
  // 从key中解析出service_name
  // key格式: /rocket/service/{service_name}/{ip:port}
  size_t prefix_pos = key.find("/rocket/service/");
  if (prefix_pos != 0) {
    return; // 不是我们关心的key格式
  }

  // 提取service_name
  size_t service_start = strlen("/rocket/service/");
  size_t service_end = key.find('/', service_start);

  if (service_end == std::string::npos) {
    return; // 格式不正确
  }

  std::string service_name =
      key.substr(service_start, service_end - service_start);

  // 计算bucket id
  auto id = nameToIndex(service_name);

  // 加锁并从缓存中移除
  std::scoped_lock<std::mutex> lock(bucket_mutex_[id]);
  auto &keyServiceMap = all_service_map_[id];

  if (keyServiceMap.find(service_name) != keyServiceMap.end()) {
    // 移除整个服务缓存，下次访问时会重新加载
    keyServiceMap.erase(service_name);
    INFOLOG("Removed service %s from cache due to etcd watch event",
            service_name.c_str());
  }
}

void EtcdRegistry::registerServicesFromConfig() {

  auto config = Config::GetGlobalConfig();
  auto instance = EtcdRegistry::GetInstance();
  for (const auto &[service_name, stub] : config->rpc_stubs_) {
    instance->registerService(service_name, stub.addr.address().to_string(),
                              stub.addr.port());
  }
}

int EtcdRegistry::nameToIndex(const std::string &service_name) {
  auto hv = std::hash<string>()(service_name);
  auto id = hv % bucket_size_;
  return id;
}

} // namespace rocket