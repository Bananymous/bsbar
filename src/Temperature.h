#pragma once

#include "Block.h"

namespace bsbar
{

	class TemperatureBlock : public Block
	{
	public:
		virtual bool add_custom_config(std::string_view key, toml::node& value) override;
		virtual bool custom_update(time_point) override;

	private:
		std::string m_temperature_path = "/sys/class/thermal/thermal_zone0/temp";
	};

}