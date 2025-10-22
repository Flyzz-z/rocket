#include "co_order_stub.h"
#include "rpc_controller.h"
#include <asio/awaitable.hpp>
#include <asio/steady_timer.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>

asio::awaitable<void>
CoOrderStub::coMakeOrder(google::protobuf::RpcController *controller,
                         const ::makeOrderRequest *request,
                         ::makeOrderResponse *response,
                         ::google::protobuf::Closure *done) {
  asio::steady_timer timer(co_await asio::this_coro::executor,
                           std::chrono::steady_clock::time_point::max());
  static_cast<rocket::RpcController *>(controller)->SetWaiter(&timer);
  makeOrder(controller, request, response, done);
  co_await timer.async_wait(asio::use_awaitable);
}
