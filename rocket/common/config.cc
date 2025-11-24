#include <asio/ip/address.hpp>
#include <tinyxml/tinyxml.h>
#include "rocket/common/config.h"



#define READ_XML_NODE(name, parent) \
TiXmlElement* name##_node = parent->FirstChildElement(#name); \
if (!name##_node) { \
  printf("Start rocket server error, failed to read node [%s]\n", #name); \
  exit(0); \
} \



#define READ_STR_FROM_XML_NODE(name, parent) \
  TiXmlElement* name##_node = parent->FirstChildElement(#name); \
  if (!name##_node|| !name##_node->GetText()) { \
    printf("Start rocket server error, failed to read config file %s\n", #name); \
    exit(0); \
  } \
  std::string name##_str = std::string(name##_node->GetText()); \



namespace rocket {


static Config* g_config = NULL;


Config* Config::GetGlobalConfig() {
  return g_config;
}

void Config::SetGlobalConfig(const char* xmlfile) {
  if (g_config == NULL) {
    if (xmlfile != NULL) {
      g_config = new Config(xmlfile);
    } else {
      g_config = new Config();
    }

  }
}

Config::~Config() {
  if (xml_document_) {
    delete xml_document_;
    xml_document_ = NULL;
  }
}

Config::Config() {
  log_level_ = "DEBUG";
}
  
Config::Config(const char* xmlfile) {
  xml_document_ = new TiXmlDocument();

  bool rt = xml_document_->LoadFile(xmlfile);
  if (!rt) {
    printf("Start rocket server error, failed to read config file %s, error info[%s] \n", xmlfile, xml_document_->ErrorDesc());
    exit(0);
  }

  READ_XML_NODE(root, xml_document_);
  READ_XML_NODE(log, root_node);
  READ_XML_NODE(server, root_node);

  READ_STR_FROM_XML_NODE(log_level, log_node);
  READ_STR_FROM_XML_NODE(log_file_name, log_node);
  READ_STR_FROM_XML_NODE(log_file_path, log_node);
  READ_STR_FROM_XML_NODE(log_max_file_size, log_node);
  READ_STR_FROM_XML_NODE(log_sync_interval, log_node);

  log_level_ = log_level_str;
  log_file_name_ = log_file_name_str;
  log_file_path_ = log_file_path_str;
  log_max_file_size_ = std::atoi(log_max_file_size_str.c_str()) ;
  log_sync_inteval_ = std::atoi(log_sync_interval_str.c_str());

  printf("LOG -- CONFIG LEVEL[%s], FILE_NAME[%s],FILE_PATH[%s] MAX_FILE_SIZE[%d B], SYNC_INTEVAL[%d ms]\n", 
    log_level_.c_str(), log_file_name_.c_str(), log_file_path_.c_str(), log_max_file_size_, log_sync_inteval_);

  READ_STR_FROM_XML_NODE(port, server_node);
  READ_STR_FROM_XML_NODE(io_threads, server_node);

  port_ = std::atoi(port_str.c_str());
  io_threads_ = std::atoi(io_threads_str.c_str());


  TiXmlElement* stubs_node = root_node->FirstChildElement("stubs");

  if (stubs_node) {
    for (TiXmlElement* node = stubs_node->FirstChildElement("rpc_server"); node; node = node->NextSiblingElement("rpc_server")) {
      RpcStub stub;
      stub.name = std::string(node->FirstChildElement("name")->GetText());
      stub.timeout = std::atoi(node->FirstChildElement("timeout")->GetText());

      std::string ip = std::string(node->FirstChildElement("ip")->GetText());
      uint16_t port = std::atoi(node->FirstChildElement("port")->GetText());
      stub.addr = tcp::endpoint(asio::ip::make_address(ip), port);

      rpc_stubs_.insert(std::make_pair(stub.name, stub));
    }
  }

  // 解析服务端提供的服务列表
  TiXmlElement* services_node = root_node->FirstChildElement("services");
  if (services_node) {
    for (TiXmlElement* node = services_node->FirstChildElement("service"); node; node = node->NextSiblingElement("service")) {
      ServiceConfig service;

      TiXmlElement* name_elem = node->FirstChildElement("name");
      TiXmlElement* ip_elem = node->FirstChildElement("ip");
      TiXmlElement* port_elem = node->FirstChildElement("port");

      if (name_elem && ip_elem && port_elem) {
        service.name = std::string(name_elem->GetText());
        service.ip = std::string(ip_elem->GetText());
        service.port = std::atoi(port_elem->GetText());

        provided_services_.push_back(service);
        printf("Loaded service config: %s at %s:%d\n",
               service.name.c_str(), service.ip.c_str(), service.port);
      }
    }
  }

	TiXmlElement* etcd_node = root_node->FirstChildElement("etcd");
	if(etcd_node) {
		std::string etcd_ip = std::string(etcd_node->FirstChildElement("ip")->GetText());
		std::string etcd_port = std::string(etcd_node->FirstChildElement("port")->GetText());
		std::string etcd_username = std::string(etcd_node->FirstChildElement("username")->GetText());
		std::string etcd_password = std::string(etcd_node->FirstChildElement("password")->GetText());

		if(etcd_ip.empty() || etcd_port.empty() || etcd_username.empty() || etcd_password.empty()) {
			printf("should config etcd ip, port, username, password");
		}

		etcd_config_.ip = etcd_ip;
		etcd_config_.port = std::atoi(etcd_port.c_str());
		etcd_config_.username = etcd_username;
		etcd_config_.password = etcd_password;
	}


  printf("Server -- PORT[%d], IO Threads[%d]\n", port_, io_threads_);

}


}