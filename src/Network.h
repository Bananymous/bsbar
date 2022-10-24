#pragma once

#include "Block.h"

namespace bsbar
{

	class NetworkBlock : public Block
	{
	public:
		virtual bool add_custom_config(std::string_view key, toml::node& value) override;
		virtual bool custom_update(time_point) override;

	private:
		std::string					m_interface;
		std::optional<std::string>	m_format_disconnected;
		std::atomic<bool>			m_color_auto = false;
	};

}