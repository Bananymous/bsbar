#include "Common.h"

#include <algorithm>
#include <cassert>

namespace bsbar
{

	void replace_all(std::string& str, std::string_view what, std::string_view to)
	{
		assert(!what.empty() && "replace_all: `what` is an empty string");
		assert(to.find(what) == std::string_view::npos && "replace_all: to contains `what`");

		std::size_t pos = 0;
		while ((pos = str.find(what, pos)) != std::string::npos)
			str.replace(pos, what.size(), to);
	}

	std::string_view get_ramp_string(double value, double min, double max, const std::vector<std::string>& ramp)
	{
		double per_ramp = (max - min) / (double)ramp.size();
		std::size_t index = std::clamp<std::size_t>(value / per_ramp, 0, ramp.size() - 1);
		return ramp[index];
	}

	std::vector<std::string_view> split(std::string_view sv, char c)
	{
		return split(sv, [c](char ch) { return ch == c; });
	}
	std::vector<std::string_view> split(std::string_view sv, const std::function<bool(char)>& comp)
	{
		std::vector<std::string_view> result;

		std::size_t pos = std::string_view::npos;
		std::size_t len = 0;

		for (std::size_t i = 0; i < sv.size(); i++)
		{
			if (comp(sv[i]))
			{
				if (pos != std::string_view::npos && len)
					result.push_back(sv.substr(pos, len));
				pos = std::string_view::npos;
				len = 0;
			}
			else
			{
				if (pos == std::string_view::npos)
					pos = i;
				len++;
			}			
		}
		
		if (pos != std::string_view::npos)
			result.push_back(sv.substr(pos));

		return result;
	}

}