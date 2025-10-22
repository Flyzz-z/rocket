#ifndef ROCKET_PROTO_CO_STUB_H
#define ROCKET_PROTO_CO_STUB_H 

#include "../order.pb.h"
#include <asio/awaitable.hpp>


class CoOrderStub : public Order_Stub {
public:
	using Order_Stub::Order_Stub;
  asio::awaitable<void> coMakeOrder(google::protobuf::RpcController* controller,
                       const ::makeOrderRequest* request,
                       ::makeOrderResponse* response,
                       ::google::protobuf::Closure* done);
};


#endif