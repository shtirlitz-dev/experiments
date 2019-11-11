#include "response.hpp"
#include "windows.h"
#include <chrono>
#include <ctime>
#include <array>
#include <functional>

using namespace std;

tuple<string_view, const char*> tokenize(const char* data, const char* data_end, char stop_char) {
  auto end = std::find(data, data_end, stop_char);
  return { end == data_end ? string_view{} : string_view{ data, size_t(end - data) }, end };
}

tuple<string_view, string_view, string_view> get_request(const char* data, size_t size) {
  // do not check whether request is valid
  auto data_end = data + size;
  auto [method, method_end] = tokenize(data, data_end, ' ');
  auto [url, url_end] = tokenize(method_end + 1, data_end, ' ');
  auto [protocol, prot_end] = tokenize(url_end + 1, data_end, '\r');
  return { method, url, protocol };
}


std::vector<char> from_resource(int id) {
  HMODULE hModule = NULL;
  GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCTSTR)&from_resource, &hModule);
  if (HRSRC fr = FindResourceA(hModule, MAKEINTRESOURCEA(id), "DAT")) {
    auto rsize = SizeofResource(hModule, fr);
    if (auto res = LoadResource(hModule, fr))
      if (const char* resource = (const char*)LockResource(res))
        return std::vector<char>(resource, resource + rsize);
  }
  return {};
}

string current_time_str() {
  using namespace std::chrono;
  std::time_t now = system_clock::to_time_t(system_clock::now());
  struct tm ptmTemp;
  std::array<char, 60> buffer;

  if (_gmtime64_s(&ptmTemp, &now) || !strftime(buffer.data(), buffer.size(), "%a, %d %b %Y %T GMT", &ptmTemp))
    buffer[0] = '\0';
  return string(buffer.data());
}

std::vector<char> replace_placeholders(const char* text, std::function<string(string_view)> replacer) {
  auto beg = text;
  auto end = strchr(beg, '\0');
  std::vector<char> page;
  while (true) {
    auto placeholder = std::find(beg, end, '%');
    page.insert(page.end(), beg, placeholder);
    if (placeholder == end)
      break;
    ++placeholder;
    auto placeholder_end = std::find(placeholder, end, '%');
    if (placeholder_end == end)
      break;
    beg = placeholder_end + 1;
    string_view ph(placeholder, size_t(placeholder_end - placeholder));
    string subst = replacer(ph);
    page.insert(page.end(), subst.begin(), subst.end());
  }
  return page;
}


std::vector<char> root_page() {
  const char* root_html = R"(
  <html>
    <header>
      <title>Right way</title>
    </header>
    <body>
      <h2>Hi, you are on the right way</h2>
      <p>Current time is: %time%</p>
      <p>Go to <a href="/many_photos">many photos</a> page.</p>
      <a href="/photo.jpg"><img src="/photo.jpg" width="600" height="325" /></a>
    <body>
  </html>)";
  return replace_placeholders(root_html, [](string_view ph) { return ph == "time" ? current_time_str() : string{}; });
}

string create_table(int cols, int rows) {
  /*
      <table cellspacing="5">
        <tr>
          <td><a href="/photo1.jpg"><img src="/photo1.jpg" width="100" height="55" alt="photo1"></a></td>
          <td><a href="/photo2.jpg"><img src="/photo2.jpg" width="100" height="55" alt="photo2"></a></td>
          ...
        </tr>
        <tr>
          ...
        </tr>
        ...
      </table>
  */
  int photo_num = 1;
  string table = R"(<table cellspacing = "5">)";
  for (int row = 0; row < rows; ++row) {
    table += R"(<tr>)";
    for (int col = 0; col < cols; ++col) {
      string num_str = std::to_string(photo_num++);
      table = table + R"(<td><a href = "/photo)" + num_str + R"(.jpg"><img src = "/photo)" +
        num_str + R"(.jpg" width="100" height="55" alt="photo)" + num_str + R"("></a></td>)";
    }
    table += R"(</tr>)";
  }
  table += R"(</table>)";
  return table;
}

std::vector<char> root_page2() {
  const char* root_html = R"(
  <html>
    <header>
      <title>Right way</title>
    </header>
    <body>
      <h2>Hi, you are on the right way</h2>
      <p>Current time is: %time%</p>
      <p>Go to <a href="/">root</a> page.</p>
      %table%
    <body>
  </html>)";
  return replace_placeholders(root_html, [](string_view ph) {
    return ph == "time" ? current_time_str() : ph == "table" ? create_table(6, 6) : string{}; });
}

std::vector<char> not_found_page(string_view url) {
  const char* html = R"(
  <html>
    <header>
      <title>Not found</title>
    </header>
    <body>
      <h2>404: page not found</h2>
      <p>The requested URL <b>%page%</b> does not exist.</p>
      <p>Go to <a href="/">root</a> page.</p>
    <body>
  </html>)";
  return replace_placeholders(html, [url](string_view ph) { return ph == "page" ? string{ url } : string{}; });
}

std::vector<char> form_answer(string_view method, string_view url, string_view protocol) {
  std::vector<char> content;
  string content_type;
  string result = "200 OK";
  if (url == "/") {
    content = root_page();
    content_type = "text/html";
  }
  else if (url == "/many_photos") {
    content = root_page2();
    content_type = "text/html";
  }
  else if (url == "/favicon.ico") {
    content = from_resource(101);
    content_type = "image/x-icon";
  }
  else if (url.starts_with("/photo") && url.ends_with(".jpg")) {
    content = from_resource(102);
    content_type = "image/jpeg";
  }
  else {
    content = not_found_page(url);
    content_type = "text/html";
    result = "404 NotFound";
  }

  string header =
    string{ protocol } +" " + result + "\r\n"
    "Cache-Control:no-cache\r\n"
    "Content-Length:" + std::to_string(content.size()) + "\r\n"
    "Content-Type:" + content_type + "\r\n"
    "Date:" + current_time_str() + "\r\n"
    "\r\n";

  std::vector<char> answer;
  answer.insert(answer.end(), header.begin(), header.end());
  answer.insert(answer.end(), content.begin(), content.end());
  return answer;
}
