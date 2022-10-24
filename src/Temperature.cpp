#include "Temperature.h"

#include "Common.h"

#include <fstream>
#include <iostream>

namespace bsbar
{

	bool TemperatureBlock::add_custom_config(std::string_view key, toml::node& value)
	{
		if (key == "thermal-zone")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			auto& thermal_zone = **value.as_string();
			m_temperature_path = "/sys/class/thermal/" + thermal_zone + "/temp";
			return true;
		}

		return false;
	}

	bool TemperatureBlock::custom_update(time_point)
	{
		std::ifstream file(m_temperature_path);
		if (file.fail())
			return false;
		
		std::string line;
		file >> line;

		double value;
		if (!string_to_value(line, value))
			return false;

		std::scoped_lock _(m_mutex);
		
		m_value.value = value / 1000.0;

		m_text = m_format;
		replace_all(m_text, "%temperature%", line.substr(0, line.size() - 3));

		return true;		
	}

}