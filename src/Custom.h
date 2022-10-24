#pragma once

#include "Block.h"

namespace bsbar
{

	class CustomBlock : public Block
	{
	public:
		virtual bool add_custom_config(std::string_view key, toml::node& value) override;
		virtual bool custom_update(time_point) override;

	private:
		std::string m_text_command;
		std::string m_value_command;
	};

}