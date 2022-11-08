#pragma once

#include "Block.h"

namespace bsbar
{

	class MenuBlock : public Block
	{
	public:
		virtual void custom_config_done() override;
		virtual bool custom_is_valid() const override;

		virtual bool custom_update(time_point tp) override;
		virtual bool custom_print() const override;
		
		virtual bool handle_custom_click(const MouseInfo& mouse, std::string_view sub) override;

		virtual bool add_custom_config(std::string_view key, toml::node& value) override;
		virtual bool add_custom_subconfig(std::string_view sub, std::string_view key, toml::node& value) override;

	private:
		struct SubMenu
		{
			std::string name;
			std::string format;
			std::string command;
			bool fork = true;
		};

		bool					m_show_submenus = false;
		std::vector<SubMenu>	m_submenus;
	};

}