#include "Battery.h"

#include "Common.h"

#include <fstream>
#include <iostream>

namespace bsbar
{

	bool BatteryBlock::add_custom_config(std::string_view key, toml::node& value)
	{
		if (key == "battery")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			auto& battery = **value.as_string();
			m_battery_path = "/sys/class/power_supply/" + battery + "/uevent";
		}

		if (key == "ramp-charging")
		{
			BSBAR_VERIFY_TYPE_CUSTOM_MESSAGE(value, array, "value for key 'ramp-charging' must be an array of strings");

			m_ramp_charging.clear();
			for (auto&& elem : *value.as_array())
			{
				BSBAR_VERIFY_TYPE_CUSTOM_MESSAGE(elem, string, "value for key 'ramp-charging' must be an array of strings");
				m_ramp_charging.push_back(**elem.as_string());
			}
			return true;
		}

		return false;
	}

	static std::unordered_map<std::string, std::string> update_battery_info(const std::string& path)
	{
		std::ifstream file(path);
		if (file.fail())
			return {};

		std::unordered_map<std::string, std::string> result;

		std::string line;
		while (std::getline(file, line))
			if (std::size_t pos = line.find('='); pos != std::string::npos)
				result[line.substr(0, pos)] = line.substr(pos + 1);

		return result;
	}

	bool BatteryBlock::custom_update(time_point)
	{
		auto battery_info = update_battery_info(m_battery_path);
		if (battery_info.empty())
			return false;
		
		double value;

		std::string_view capacity_sv;
		if (auto it = battery_info.find("POWER_SUPPLY_CAPACITY"); it != battery_info.end())
		{
			capacity_sv = it->second;
			if (!string_to_value(capacity_sv, value))
				return false;
		}
		else
			return false;

		std::string_view status_sv;
		if (auto it = battery_info.find("POWER_SUPPLY_STATUS"); it != battery_info.end())
			status_sv = it->second;
		else
			return false;


		std::scoped_lock _(m_mutex);

		m_value.value = value;
		m_text = m_format;

		replace_all(m_text, "%percentage%",	"%value%");
		replace_all(m_text, "%status%",		status_sv);

		if (status_sv == "Charging" && !m_ramp_charging.empty())
		{
			auto ramp_sv = get_ramp_string(m_value.value, m_value.min, m_value.max, m_ramp_charging);
			replace_all(m_text, "%ramp%", ramp_sv);
		}

		return true;
	}

}