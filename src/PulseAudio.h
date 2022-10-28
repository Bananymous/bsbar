#pragma once

#include "Block.h"

namespace bsbar
{

	class PulseAudioBlock : public Block
	{
		virtual bool custom_update(time_point tp) override;
		virtual bool add_custom_config(std::string_view key, toml::node& value) override;
		virtual bool handle_custom_scroll(const MouseInfo& mouse) override;
	};

}