#include "Menu.h"

#include "Common.h"

#include <algorithm>
#include <iostream>

namespace bsbar
{

	void MenuBlock::custom_initialize()
	{
		for (auto& submenu : m_submenus)
			submenu->initialize();
	}

	void MenuBlock::custom_config_done()
	{
		std::sort(m_submenus.begin(), m_submenus.end(), [](const auto& a, const auto& b) { return a->get_instance() < b->get_instance(); });
	}

	bool MenuBlock::custom_is_valid() const
	{
		for (auto& submenu : m_submenus)
			if (!submenu->is_valid())
				return false;
		return true;
	}

	bool MenuBlock::custom_update(time_point tp)
	{
		for (auto& submenu : m_submenus)
			;

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
			submenu->print();
			std::printf(",");
		}

		return true;
	}

	bool MenuBlock::handle_custom_click(const MouseInfo& mouse, std::string_view sub)
	{
		std::scoped_lock _(m_mutex);
		if (sub.empty())
		{
			if (mouse.type != MouseType::Left)
				return true;
			m_show_submenus = !m_show_submenus;
		}
		else
		{
			for (auto& submenu : m_submenus)
			{
				if (submenu->get_instance().substr(submenu->get_instance().find('.') + 1) == sub)
				{
					submenu->handle_click(mouse, "");
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

	bool MenuBlock::add_custom_subconfig(std::string_view sub, toml::table& table)
	{
		if (sub.size() < 4 || sub.substr(0, 3) != "sub" || sub.substr(3).find_first_not_of("0123456789") != std::string_view::npos)
			return false;

		std::string sub_name = m_name + '.' + std::string(sub);
		m_submenus.push_back(Block::create(sub_name, table));
		assert(m_submenus.back().get());

		return true;
	}

}