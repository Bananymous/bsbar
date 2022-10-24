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

		std::string_view	value_sv;
		double				value;

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

			value_sv = buffer;
			value_sv = value_sv.substr(0, value_sv.find('\n'));
			if (!string_to_value(value_sv, value))
				return false;
		}

		std::scoped_lock _(m_mutex);

		m_text = m_format;

		if (!text.empty())
			replace_all(m_text, "%text%", text);

		if (!value_sv.empty())
		{
			m_value.value = value;
			replace_all(m_text, "%value%", value_sv);
		}

		return true;
	}

}