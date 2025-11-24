#ifndef ROCKET_NET_RPC_ETCD_REGISTRY_H
#define ROCKET_NET_RPC_ETCD_REGISTRY_H

#include "etcd/Client.hpp"
#include "singleton.h"
#include "spinlock.h"
#include <etcd/KeepAlive.hpp>
#include <etcd/Watcher.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <thread>
#include <atomic>

namespace rocket {

class EtcdRegistry : public Singleton<EtcdRegistry> {
public:
  EtcdRegistry();
  ~EtcdRegistry();

  // 服务端初始化: 会注册本地服务到etcd
  static void initAsServer(const std::string &ip, int port,
                           const std::string &username,
                           const std::string &password,
                           const std::string &service_name,
                           const std::string &service_ip,
                           int service_port);

  // 服务端从配置文件初始化: 自动注册配置中的所有服务
  static void initAsServerFromConfig();

  // 客户端初始化: 仅连接etcd用于服务发现
  static void initAsClient(const std::string &ip, int port,
                           const std::string &username,
                           const std::string &password);

  bool registerService(const std::string &service_name,
                       const std::string &service_ip, int service_port);

  void unregisterService(const std::string &service_name);

  std::vector<std::string> discoverService(const std::string &service_name);
	
	// 根据key获取服务地址
  std::vector<std::string> loadByKey(const std::string &service_name);

  // 启动watcher监听服务变化
  void startWatcher();
  
  // 停止watcher
  void stopWatcher();

private:
  int nameToIndex(const std::string &service_name);

  // 处理watch事件
  void handleWatchEvent(etcd::Response response);
  
  // 从缓存中移除失效服务
  void removeServiceFromCache(const std::string& key);

  std::string name_;
  std::string ip_;
  int port_;
  std::string username_;
  std::string password_;
  std::unique_ptr<etcd::Client> etcd_client_;  // 用于服务注册和查询
  std::unordered_map<std::string, std::shared_ptr<etcd::KeepAlive>>
      keep_alives_;

  /*
          ac使用atomic操作
          bk使用lock
   */
  const int bucket_size_ = 8;
  using service_map = std::unordered_map<std::string, std::vector<std::string>>;
  std::vector<service_map> all_service_map_;
  std::vector<AdaptiveSpinLock> bucket_lock_;

  // Watcher相关成员
  std::unique_ptr<etcd::Client> watcher_client_;  // watcher 专用 client
  std::unique_ptr<etcd::Watcher> watcher_;
  std::atomic<bool> watching_;
  std::unique_ptr<std::thread> watcher_thread_;
};

} // namespace rocket

#endif