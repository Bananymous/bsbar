#pragma once

#include "Block.h"

namespace bsbar
{

	class BatteryBlock : public Block
	{
	public:
		virtual bool add_custom_config(std::string_view key, toml::node& value) override;
		virtual bool custom_update(time_point) override;

	private:
		std::string					m_battery_path = "/sys/class/power_supply/BAT0/uevent";
		std::vector<std::string>	m_ramp_charging;
	};

}