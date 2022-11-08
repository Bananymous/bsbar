#include "Menu.h"

#include "Common.h"

#include <algorithm>
#include <iostream>

namespace bsbar
{

	void MenuBlock::custom_config_done()
	{
		std::sort(m_submenus.begin(), m_submenus.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
	}

	bool MenuBlock::custom_is_valid() const
	{
		for (auto& submenu : m_submenus)
		{
			if (submenu.format.empty())
			{
				std::cerr << "No format specified for module '" << m_name << '.' << submenu.name << '\'' << std::endl;
				return false;
			}
		}

		return true;
	}

	bool MenuBlock::custom_update(time_point tp)
	{
		std::scoped_lock _(m_mutex);
		m_text = m_format;
		return true;
	}

	bool MenuBlock::custom_print() const
	{
		if (!m_show_submenus)
			return true;

		for (const auto& submenu : m_submenus)
		{
			std::printf("{");
			std::printf( "\"name\":\"%s\"",			m_type.c_str());
			std::printf(",\"instance\":\"%s.%s\"",	m_name.c_str(), submenu.name.c_str());
			std::printf(",\"full_text\":\"%s\"",	submenu.format.c_str());
			std::printf(",\"separator\":false");
			std::printf("},");
		}

		return true;
	}

	bool MenuBlock::handle_custom_click(const MouseInfo& mouse, std::string_view sub)
	{
		if (mouse.type != MouseType::Left)
			return true;

		std::scoped_lock _(m_mutex);
		if (sub.empty())
		{
			m_show_submenus = !m_show_submenus;
		}
		else
		{
			for (auto& submenu : m_submenus)
			{
				if (submenu.name == sub)
				{
					if (submenu.fork)
					{
						if (fork() == 0)
						{
							close(STDOUT_FILENO);
							execl("/bin/sh", "sh", "-c", submenu.command.c_str(), NULL);
						}
					}
					else
					{
						std::system((submenu.command + " > /dev/null").c_str());
					}
				}
			}
		}

		return true;
	}

	bool MenuBlock::add_custom_config(std::string_view key, toml::node& value)
	{
		if (key == "show-default")
		{
			BSBAR_VERIFY_TYPE(value, boolean, key);
			m_show_submenus = **value.as_boolean();
			return true;
		}

		return false;
	}

	bool MenuBlock::add_custom_subconfig(std::string_view sub, std::string_view key, toml::node& value)
	{
		if (sub.size() < 4 || sub.substr(0, 3) != "sub" || sub.substr(3).find_first_not_of("0123456789") != std::string_view::npos)
			return false;

		auto it = std::find_if(m_submenus.begin(), m_submenus.end(), [sub](auto submenu) { return submenu.name == sub; });
		if (it == m_submenus.end())
			it = m_submenus.insert(m_submenus.end(), { .name = std::string(sub) });

		if (key == "format")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			it->format = **value.as_string();
			return true;
		}
		else if (key == "command")
		{
			BSBAR_VERIFY_TYPE(value, string, key);
			it->command = **value.as_string();
			return true;
		}
		else if (key == "fork")
		{
			BSBAR_VERIFY_TYPE(value, boolean, key);
			it->fork = **value.as_boolean();
			return true;
		}
		else
		{
			std::cerr << "Unknown key '" << key << "' for module '" << m_name << '.' << sub << '\'' << std::endl;
			std::cerr << "  " << value.source() << std::endl;
			exit(1);	
		}

		return false;
	}

}