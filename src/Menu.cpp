#include "Menu.h"

#include "Common.h"

#include <algorithm>
#include <iostream>

namespace bsbar
{

	void MenuBlock::custom_initialize()
	{
		std::sort(m_submenus.begin(), m_submenus.end(), [](const auto& a, const auto& b) { return a->get_instance() < b->get_instance(); });
		for (auto& submenu : m_submenus)
			submenu->initialize();
	}

	void MenuBlock::custom_tick()
	{
		if (!m_show_submenus)
			return;
		if (++m_timeout_ticks % m_timeout)
			return;
		m_show_submenus = false;
		m_timeout_ticks = 0;
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
		std::scoped_lock _(m_mutex);
		m_text = m_format;
		for (auto& submenu : m_submenus)
			submenu->m_i3bar["separator"] = { .is_string = false, .value = "false" };
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

		if (key == "timeout")
		{
			BSBAR_VERIFY_TYPE(value, integer, key);
			int64_t timeout = **value.as_integer();
			if (timeout == 0)
				m_timeout = UINT64_MAX;
			else if (timeout > 0)
				m_timeout = timeout;
			else
			{
				std::cerr << "Value for key 'timeout' must be positive integer or 0 to disable timeout" << std::endl;
				std::cerr << "  " << value.source() << std::endl;
				exit(1);
			}
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