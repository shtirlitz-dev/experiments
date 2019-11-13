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

auto curr_thread_id() {
  // assigns progressive number to every thread
  static thread_local int th_num = 0;
  static std::atomic_int last_th_num = 0;
  if (th_num == 0)
    th_num = ++last_th_num;
  return th_num;
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
std::condition_variable log_check;
std::vector<string> log_entries;
std::atomic_bool close_log = false;

class Log
{
  std::stringstream sstream;
public:
  Log(int conn_number = 0) {
    sstream << "[thread " << curr_thread_id() << "] ";
    if (conn_number)
      sstream << "connection " << conn_number << ": ";
  }
  template<class T>
  auto& operator<<(T&& arg) {
    return sstream << std::forward<T>(arg);
  }
  ~Log() {
    sstream << std::endl;
    std::lock_guard locker(mx_log);
    log_entries.push_back(sstream.str());
    log_check.notify_one();
  }
};

auto log_getter() {
  std::vector<string> entries;
  while (true) {
    for (auto& item : entries)
      co_yield item;
    entries.clear();
    if (close_log)
      break;
    // wait for the next entries
    std::unique_lock<std::mutex> locker(mx_log);
    if (log_entries.empty())
      log_check.wait(locker);
    std::swap(entries, log_entries); // mutex is locked here
  }
}

auto make_http_handler(asio::ip::tcp::socket socket, int conn_number) {
  return [socket = std::move(socket), conn_number]() mutable -> asio::awaitable<void> {
    std::array<char, 1024> buffer;
    while (true) {
      std::error_code ec;
      const auto size = co_await socket.async_read_some(asio::buffer(buffer), use_awaitable(ec));
      if (ec) {
        if (ec.value()==2)
          Log(conn_number) << "closed";
        else
          Log(conn_number) << "recv error: " << ec.message() << " (" << ec.value() << ")";
        break;
      }
      auto [method, url, protocol] = get_request(buffer.data(), size);
      Log(conn_number) << "received: " << method << " " << url << " " << protocol;
      auto answer = form_answer(method, url, protocol);
      auto write_res = co_await asio::async_write(socket, asio::buffer(answer.data(), answer.size()), use_awaitable(ec));
      if (ec) {
        Log(conn_number) << "send error: " << ec.message() << " (" << ec.value() << ")";
        break;
      }
      Log(conn_number) << write_res << " bytes written";
    }
  };
}

auto server(asio::ip::tcp::endpoint endpoint) {
  return [endpoint]() -> asio::awaitable<void> {
    auto executor = co_await asio::this_coro::executor;
    auto ip_and_port = endpoint.address().to_string() + ":" + std::to_string(endpoint.port());
    try {
      asio::ip::tcp::acceptor acceptor{ executor, endpoint, false };
      Log() << "server starts on " << ip_and_port;
      while (true) {
        std::error_code ec;
        // wait for incoming connection
        auto socket = co_await acceptor.async_accept(use_awaitable(ec));
        if (ec) {
          Log() << "accept error: " << ec.message() << " (" << ec.value() << ")";
          continue;
        }
        static std::atomic_int number{ 0 };
        int conn_number = ++number;
        Log(conn_number) << "on port " << endpoint.port() << " from " << socket.remote_endpoint().address().to_string();
        co_spawn(executor, make_http_handler(std::move(socket), conn_number), detach_rethrow);
      }
    }
    catch (asio::system_error & ex) {
      Log() << "server on " << ip_and_port << " error:" << ex.code();
    }
  };
}


int main() {
  try {
    int work_thread_count = std::thread::hardware_concurrency();
    if (!work_thread_count)
      work_thread_count = 1;
    std::cout << work_thread_count << " working threads" << std::endl;
    asio::io_context context{ work_thread_count };

    asio::signal_set signals{ context, SIGINT, SIGTERM };
    signals.async_wait([&](auto, auto) {
      close_log = true;
      Log() << "terminating...";
      context.stop();
    });

    const char* host = "127.0.0.1";
    auto endpoint = asio::ip::tcp::resolver{ context }.resolve(host, "8888")->endpoint();
    asio::co_spawn(context, server(endpoint), detach_rethrow);
    auto endpoint2 = asio::ip::tcp::resolver{ context }.resolve(host, "7777")->endpoint();
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
}

