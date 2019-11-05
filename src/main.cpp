#include "main.hpp"
#include <asio.hpp>
#include <fmt/format.h>
#include <array>

auto use_awaitable() {
  return asio::use_awaitable;
}

auto use_awaitable(std::error_code& ec) {
  return asio::redirect_error(asio::use_awaitable, ec);
}

void detach_rethrow(std::exception_ptr e) {
  if (e) {
    std::rethrow_exception(e);
  }
}

auto echo(asio::ip::tcp::socket socket) {
  return [socket = std::move(socket)]() mutable -> asio::awaitable<void> {
    std::array<char, 1024> buffer;
    while (true) {
      std::error_code ec;
      const auto size = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable(ec));
      if (ec) {
        fmt::print(stderr, "recv error: {} ({})\n", ec.message(), ec.value());
        break;
      }
      co_await asio::async_write(socket, asio::buffer(buffer.data(), size), use_awaitable(ec));
      if (ec) {
        fmt::print(stderr, "send error: {} ({})\n", ec.message(), ec.value());
        break;
      }
    }
  };
}

auto server(asio::ip::tcp::endpoint endpoint) {
  return [endpoint]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;
    asio::ip::tcp::acceptor acceptor{ executor, endpoint };
    while (true) {
      std::error_code ec;
      auto socket = co_await acceptor.async_accept(use_awaitable(ec));
      if (ec) {
        fmt::print(stderr, "accept error: {} ({})\n", ec.message(), ec.value());
        continue;
      }
      co_spawn(executor, echo(std::move(socket)), detach_rethrow);
    }
  };
}

int main() {
  try {
    asio::io_context context{ 1 };

    asio::signal_set signals{ context, SIGINT, SIGTERM };
    signals.async_wait([&](auto, auto) { context.stop(); });

    auto endpoint = asio::ip::tcp::resolver{ context }.resolve("127.0.0.1", "9000")->endpoint();
    asio::co_spawn(context, server(endpoint), detach_rethrow);

    context.run();
  }
  catch (std::exception& e) {
    fmt::print(stderr, "error: {}\n", e.what());
  }
}
