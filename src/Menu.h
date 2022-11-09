#pragma once

#include "Block.h"

namespace bsbar
{

	class MenuBlock : public Block
	{
	public:
		virtual void custom_initialize() override;

		virtual void custom_tick() override;

		virtual bool custom_is_valid() const override;

		virtual bool custom_update(time_point tp) override;
		virtual bool custom_print() const override;
		
		virtual bool handle_custom_click(const MouseInfo& mouse, std::string_view sub) override;

		virtual bool add_custom_config(std::string_view key, toml::node& value) override;
		virtual bool add_custom_subconfig(std::string_view sub, toml::table& table) override;

	private:
		std::atomic<uint64_t>				m_timeout		= UINT64_MAX;
		std::atomic<uint64_t>				m_timeout_ticks	= 0;

		std::atomic<bool>					m_show_submenus	= false;
		std::vector<std::unique_ptr<Block>>	m_submenus;
	};

}