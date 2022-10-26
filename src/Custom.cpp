#include "Custom.h"

#include "Common.h"

#include <iostream>

namespace bsbar
{

	bool CustomBlock::add_custom_config(std::string_view key, toml::node& value)
	{
		if (key == "text-command")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			m_text_command = **value.as_string();
			return true;
		}

		if (key == "value-command")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			m_value_command = **value.as_string();
			return true;
		}

		return false;
	}

	bool CustomBlock::custom_update(time_point)
	{
		std::string_view text;

		constexpr double nan = std::numeric_limits<double>::quiet_NaN();

		double value = nan;

		if (!m_text_command.empty())
		{
			FILE* fp = popen(m_text_command.c_str(), "r");
			if (fp == NULL)
				return false;

			char buffer[128];
			bool success = fgets(buffer, sizeof(buffer), fp) != NULL;

			pclose(fp);

			if (!success)
				return false;

			text = buffer;
			text = text.substr(0, text.find('\n'));
		}

		if (!m_value_command.empty())
		{
			FILE* fp = popen(m_value_command.c_str(), "r");
			if (fp == NULL)
				return false;

			char buffer[128];
			bool success = fgets(buffer, sizeof(buffer), fp) != NULL;

			pclose(fp);

			if (!success)
				return false;

			if (!string_to_value(buffer, value))
				return false;
		}

		std::scoped_lock _(m_mutex);

		if (value != nan)
			m_value.value = value;

		m_text = m_format;

		if (!text.empty())
			replace_all(m_text, "%text%", text);
		else
			replace_all(m_text, "%text", "<error>");

		return true;
	}

}