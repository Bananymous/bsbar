#pragma once

#include "Block.h"

namespace bsbar
{

	class PulseAudioBlock : public Block
	{
	public:
		PulseAudioBlock();

		virtual void custom_initialize() override;

		virtual bool custom_update(time_point tp) override;
		virtual bool add_custom_config(std::string_view key, toml::node& value) override;

		virtual bool handle_custom_click(const MouseInfo& mouse) override;
		virtual bool handle_custom_scroll(const MouseInfo& mouse) override;

	private:
		std::optional<std::string>	m_format_muted;
		std::optional<std::string>	m_color_muted;
		uint32_t					m_max_volume;
		uint32_t					m_volume_step;
		bool						m_verify_volume = true;
		bool						m_enable_scroll = true;
		bool						m_click_to_mute = true;
	};

	class PulseAudioInputBlock : public Block
	{
	public:
		virtual void custom_initialize() override;

		virtual bool custom_update(time_point tp) override;
		virtual bool add_custom_config(std::string_view key, toml::node& value) override;

		virtual bool handle_custom_click(const MouseInfo& mouse) override;

	private:
		std::optional<std::string>	m_format_muted;
		std::optional<std::string>	m_color_muted;
		bool						m_click_to_mute = true;
	};

}