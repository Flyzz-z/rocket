#include <asio/buffer.hpp>
#include <vector>
#include <string.h>
#include <arpa/inet.h>
#include "rocket/net/coder/tinypb_coder.h"
#include "rocket/net/coder/tinypb_protocol.h"
#include "rocket/common/util.h"
#include "rocket/common/log.h"

namespace rocket {

// 将 message 对象转化为字节流，写入到 buffer
void TinyPBCoder::encode(std::vector<AbstractProtocol::s_ptr>& messages, TcpBuffer& out_buffer) {
  for (auto &i : messages) {
    std::shared_ptr<TinyPBProtocol> msg = std::dynamic_pointer_cast<TinyPBProtocol>(i);
    int len = 0;
    const char* buf = encodeTinyPB(msg, len);
    if (buf != NULL && len != 0) {
      out_buffer.writeToBuffer(buf, len);
    }
    if (buf) {
      free((void*)buf);
      buf = NULL;
    }

  }
}

// 将 buffer 里面的字节流转换为 message 对象
void TinyPBCoder::decode(std::vector<AbstractProtocol::s_ptr>& out_messages, TcpBuffer& buffer) {
  while(1) {
    // 遍历 buffer，找到 PB_START，找到之后，解析出整包的长度。然后得到结束符的位置，判断是否为 PB_END
    std::vector<char> tmp = buffer.getBufferVecCopy();
    int start_index = 0;
    int end_index = -1;

    int pk_len = 0;
    bool parse_success = false;
    int i = 0;
    for (i = start_index; i < tmp.size(); ++i) {
      if (tmp[i] == TinyPBProtocol::PB_START) {
        // 读下去四个字节。由于是网络字节序，需要转为主机字节序  
        if (i + 1 < tmp.size()) {
          pk_len = getInt32FromNetByte(&tmp[i+1]);
          DEBUGLOG("get pk_len = %d", pk_len);

          // 结束符的索引
          int j = i + pk_len - 1;
          if (j >= tmp.size()) {
            continue;
          }
          if (tmp[j] == TinyPBProtocol::PB_END) {
            start_index = i;
            end_index = j;
            parse_success = true;
            break;
          }
          
        }
      }
    }

    if (i >= tmp.size()) {
      DEBUGLOG("decode end, read all buffer data");
      return;
    }

    if (parse_success) {
      buffer.consume(end_index - start_index + 1);
      std::shared_ptr<TinyPBProtocol> message = std::make_shared<TinyPBProtocol>(); 
      message->pk_len_ = pk_len;

      int msg_id_len_index = start_index + sizeof(char) + sizeof(message->pk_len_);
      if (msg_id_len_index >= end_index) {
        message->parse_success = false;
        ERRORLOG("parse error, msg_id_len_index[%d] >= end_index[%d]", msg_id_len_index, end_index);
        continue;
      }
      message->msg_id_len_ = getInt32FromNetByte(&tmp[msg_id_len_index]);
      DEBUGLOG("parse msg_id_len=%d", message->msg_id_len_);

      int msg_id_index = msg_id_len_index + sizeof(message->msg_id_len_);
      
      char msg_id[100] = {0};
      memcpy(&msg_id[0], &tmp[msg_id_index], message->msg_id_len_);
      message->msg_id_ = std::string(msg_id);
      DEBUGLOG("parse msg_id=%s", message->msg_id_.c_str());

      int method_name_len_index = msg_id_index + message->msg_id_len_;
      if (method_name_len_index >= end_index) {
        message->parse_success = false;
        ERRORLOG("parse error, method_name_len_index[%d] >= end_index[%d]", method_name_len_index, end_index);
        continue;
      }
      message->method_name_len_ = getInt32FromNetByte(&tmp[method_name_len_index]);

      int method_name_index = method_name_len_index + sizeof(message->method_name_len_);
      char method_name[512] = {0};
      memcpy(&method_name[0], &tmp[method_name_index], message->method_name_len_);
      message->method_name_ = std::string(method_name);
      DEBUGLOG("parse method_name=%s", message->method_name_.c_str());

      int err_code_index = method_name_index + message->method_name_len_;
      if (err_code_index >= end_index) {
        message->parse_success = false;
        ERRORLOG("parse error, err_code_index[%d] >= end_index[%d]", err_code_index, end_index);
        continue;
      }
      message->err_code_ = getInt32FromNetByte(&tmp[err_code_index]);


      int error_info_len_index = err_code_index + sizeof(message->err_code_);
      if (error_info_len_index >= end_index) {
        message->parse_success = false;
        ERRORLOG("parse error, error_info_len_index[%d] >= end_index[%d]", error_info_len_index, end_index);
        continue;
      }
      message->err_info_len_ = getInt32FromNetByte(&tmp[error_info_len_index]);

      int err_info_index = error_info_len_index + sizeof(message->err_info_len_);
      char error_info[512] = {0};
      memcpy(&error_info[0], &tmp[err_info_index], message->err_info_len_);
      message->err_info_ = std::string(error_info);
      DEBUGLOG("parse error_info=%s", message->err_info_.c_str());

      int pb_data_len = message->pk_len_ - message->method_name_len_ - message->msg_id_len_ - message->err_info_len_ - 2 - 24;

      int pd_data_index = err_info_index + message->err_info_len_;
      message->pb_data_ = std::string(&tmp[pd_data_index], pb_data_len);

      // 这里校验和去解析
      message->parse_success = true;

      out_messages.push_back(message);
    }

  }


}


const char* TinyPBCoder::encodeTinyPB(std::shared_ptr<TinyPBProtocol> message, int& len) {
  if (message->msg_id_.empty()) {
    message->msg_id_ = "123456789";
  }
  DEBUGLOG("msg_id = %s", message->msg_id_.c_str());
  int pk_len = 2 + 24 + message->msg_id_.length() + message->method_name_.length() + message->err_info_.length() + message->pb_data_.length();
  DEBUGLOG("pk_len = %d", pk_len);

  char* buf = reinterpret_cast<char*>(malloc(pk_len));
  char* tmp = buf;

  *tmp = TinyPBProtocol::PB_START;
  tmp++;

  int32_t pk_len_net = htonl(pk_len);
  memcpy(tmp, &pk_len_net, sizeof(pk_len_net));
  tmp += sizeof(pk_len_net);

  int msg_id_len = message->msg_id_.length();
  int32_t msg_id_len_net = htonl(msg_id_len);
  memcpy(tmp, &msg_id_len_net, sizeof(msg_id_len_net));
  tmp += sizeof(msg_id_len_net);

  if (!message->msg_id_.empty()) {
    memcpy(tmp, &(message->msg_id_[0]), msg_id_len);
    tmp += msg_id_len;
  }

  int method_name_len = message->method_name_.length();
  int32_t method_name_len_net = htonl(method_name_len);
  memcpy(tmp, &method_name_len_net, sizeof(method_name_len_net));
  tmp += sizeof(method_name_len_net);

  if (!message->method_name_.empty()) {
    memcpy(tmp, &(message->method_name_[0]), method_name_len);
    tmp += method_name_len;
  }

  int32_t err_code_net = htonl(message->err_code_);
  memcpy(tmp, &err_code_net, sizeof(err_code_net));
  tmp += sizeof(err_code_net);

  int err_info_len = message->err_info_.length();
  int32_t err_info_len_net = htonl(err_info_len);
  memcpy(tmp, &err_info_len_net, sizeof(err_info_len_net));
  tmp += sizeof(err_info_len_net);

  if (!message->err_info_.empty()) {
    memcpy(tmp, &(message->err_info_[0]), err_info_len);
    tmp += err_info_len;
  }

  if (!message->pb_data_.empty()) {
    memcpy(tmp, &(message->pb_data_[0]), message->pb_data_.length());
    tmp += message->pb_data_.length();
  }

  int32_t check_sum_net = htonl(1);
  memcpy(tmp, &check_sum_net, sizeof(check_sum_net));
  tmp += sizeof(check_sum_net);

  *tmp = TinyPBProtocol::PB_END;

  message->pk_len_ = pk_len;
  message->msg_id_len_ = msg_id_len;
  message->method_name_len_ = method_name_len;
  message->err_info_len_ = err_info_len;
  message->parse_success = true;
  len = pk_len;

  DEBUGLOG("encode message[%s] success", message->msg_id_.c_str());

  return buf;
}


}