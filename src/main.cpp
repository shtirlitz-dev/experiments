#include "main.hpp"
#include "response.hpp"
#include <asio.hpp>
#include <fmt/format.h>
#include <array>
#include <atomic>
#include <iostream>
#include <utility>
#include <experimental/generator>

using std::string;
using std::string_view;
using std::tuple;

auto current_thread() {
  return ::GetCurrentThreadId();
}

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

std::mutex mx_log;
std::vector<string> log_entries;
HANDLE hLogEvent = nullptr;
std::atomic_bool close_log = false;

template<typename ...Args>
void Log(Args&& ... args)
{
  std::stringstream sstream;
  sstream << "[" << current_thread() << "] ";
  ((sstream << std::forward<Args>(args)), ...);
  sstream << std::endl;
  std::lock_guard lg(mx_log);
  log_entries.push_back(sstream.str());
  SetEvent(hLogEvent);
}

std::experimental::generator<string> log_getter() {
  std::vector<string> entries;
  while (!close_log) {
    WaitForSingleObject(hLogEvent, INFINITE);
    entries.clear();
    if (std::lock_guard lg(mx_log); true)
      std::swap(entries, log_entries);
    for (auto& item : entries)
      co_yield item;
  }
}

auto echo(asio::ip::tcp::socket socket, int conn_number) {
  return [socket = std::move(socket), conn_number]() mutable -> asio::awaitable<void> {
    std::array<char, 1024> buffer;
    while (true) {
      std::error_code ec;
      const auto size = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable(ec));
      if (ec) {
        Log(conn_number, ": recv error: ", ec.message(), " (", ec.value(), ")");
        break;
      }
      auto [method, url, protocol] = get_request(buffer.data(), size);
      Log(conn_number, ": received: ", method, " ", url, " ", protocol);
      auto answer = form_answer(method, url, protocol);
      auto write_res = co_await asio::async_write(socket, asio::buffer(answer.data(), answer.size()), use_awaitable(ec));
      if (ec) {
        Log(conn_number, ": send error: ", ec.message(), " (", ec.value(), ")");
        break;
      }
      Log(conn_number, ": written: ", write_res, " bytes");
    }
  };
}

auto server(asio::ip::tcp::endpoint endpoint) {
  return [endpoint]() -> asio::awaitable<void> {
    Log("server starts on ", endpoint.address().to_string(), ":", endpoint.port());
    auto executor = co_await asio::this_coro::executor;
    asio::ip::tcp::acceptor acceptor{ executor, endpoint };
    while (true) {
      std::error_code ec;
      // wait for incoming connection
      auto socket = co_await acceptor.async_accept(use_awaitable(ec));
      if (ec) {
        fmt::print(stderr, "accept error: {} ({})\n", ec.message(), ec.value());
        continue;
      }
      static std::atomic_int number{ 0 };
      int conn_number = ++number;
      Log("port ", endpoint.port(), " - connection ", conn_number, " from ", socket.remote_endpoint().address().to_string());
      co_spawn(executor, echo(std::move(socket), conn_number), detach_rethrow);
    }
  };
}


int main() {
  hLogEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

  try {
    int work_thread_count = std::thread::hardware_concurrency();
    std::cout << work_thread_count << " working threads" << std::endl;
    asio::io_context context{ work_thread_count };

    asio::signal_set signals{ context, SIGINT, SIGTERM };
    signals.async_wait([&](auto, auto) {
      close_log = true;
      Log("terminating...");
      context.stop();
    });

    //auto endpoint = asio::ip::tcp::resolver{ context }.resolve("127.0.0.1", "9000")->endpoint();
    auto endpoint = asio::ip::tcp::resolver{ context }.resolve("192.168.178.39", "8080")->endpoint();
    asio::co_spawn(context, server(endpoint), detach_rethrow);
    auto endpoint2 = asio::ip::tcp::resolver{ context }.resolve("192.168.178.39", "8000")->endpoint();
    asio::co_spawn(context, server(endpoint2), detach_rethrow);

    // context.run(); in multiple threads
    std::vector<std::thread> threads;
    for (int i = 0; i < work_thread_count; ++i)
      threads.emplace_back([&context] {context.run(); });

    // get log entries as they appear and put them in std output
    for (auto& log : log_getter())
      std::cout << log;

    for (auto& th : threads)
      th.join();
  }
  catch (std::exception& e) {
    fmt::print(stderr, "error: {}\n", e.what());
  }
  CloseHandle(hLogEvent);
}

