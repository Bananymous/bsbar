#pragma once

#include <charconv>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#define BSBAR_VERIFY_TYPE(value, type, key) \
	if (!value.is_##type()) { std::cerr << "value for key '" << key << "' must be a " << #type << "\n  " << value.source() << std::endl; exit(1); }

#define BSBAR_VERIFY_TYPE_CUSTOM_MESSAGE(value, type, message) \
	if (!value.is_##type()) { std::cerr << message << "\n  " << value.source() << std::endl; exit(1); }

namespace bsbar
{

	void replace_all(std::string& str, std::string_view what, std::string_view to);

	std::string_view get_ramp_string(double value, double min, double max, const std::vector<std::string>& ramp);

	std::string value_to_string(double value, int percision);

	std::vector<std::string_view> split(std::string_view sv, char c);
	std::vector<std::string_view> split(std::string_view sv, const std::function<bool(char)>& comp);

	template<typename T>
	T map(T x, T a, T b, T c, T d)
	{
		return (x - a) / (b - a) * (d - c) + c;
	}

	template<typename T>
	bool string_to_value(std::string_view str, T& out)
	{
		return std::from_chars(str.data(), str.data() + str.size(), out).ec == std::errc();
	}

}