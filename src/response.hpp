#pragma once
#include <tuple>
#include <string>
#include <string_view>
#include <vector>

// returns {method, url, protocol}
std::tuple<std::string_view, std::string_view, std::string_view> get_request(const char* data, size_t size);

std::vector<char> form_answer(std::string_view method, std::string_view url, std::string_view protocol);
