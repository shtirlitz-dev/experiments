#pragma once
#if defined(__INTELLISENSE__) && defined(ASIO_HAS_CO_AWAIT)
#ifndef BOOST_ASIO_THIS_CORO_HPP
#define BOOST_ASIO_THIS_CORO_HPP
#include <asio/executor.hpp>
#include <experimental/coroutine>
namespace asio::this_coro {
class executor_t {
public:
  constexpr executor_t() noexcept = default;
  constexpr bool await_ready() const noexcept {
    return true;
  }
  bool await_suspend(std::experimental::coroutine_handle<> handle) const noexcept {
    return false;
  }
  asio::executor await_resume() const noexcept {
    return {};
  }
};
constexpr executor_t executor;
}  // namespace asio::this_coro
#endif
#endif
